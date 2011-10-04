/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


#if !defined(_IO_HBA_QLA2100_H)		/* wrapper symbol for kernel use */
#define _IO_HBA_QLA2100_H		/* subject to change without notice */

#if !defined(LINUX_VERSION_CODE)
#include <linux/version.h>
#endif  /* LINUX_VERSION_CODE not defined */

#if !defined(HOSTS_C)

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Driver debug definitions.
 */
/* #define QL_DEBUG_LEVEL_1  */ /* Output register accesses to COM1 */
/* #define QL_DEBUG_LEVEL_2  */ /* Output error msgs to COM1 */
/* #define QL_DEBUG_LEVEL_3  */ /* Output function trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_4  */ /* Output NVRAM trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_5  */ /* Output ring trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_6  */ /* Output WATCHDOG timer trace to COM1 */
/* #define QL_DEBUG_LEVEL_7  */ /* Output RISC load trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_8  */ /* Output ring saturation msgs to COM1 */
/* #define QL_DEBUG_LEVEL_9  */ /* Output IOCTL trace msgs */
/* #define QL_DEBUG_LEVEL_10 */ /* Output IOCTL error msgs */
/* #define QL_DEBUG_LEVEL_11 */ /* Output Mbx Cmd trace msgs */
/* #define QL_DEBUG_LEVEL_12 */ /* Output IP trace msgs */
/* #define QL_DEBUG_LEVEL_13 */ /* Output fdmi function trace msgs */

#define QL_DEBUG_CONSOLE            /* Output to console */

#include <asm/bitops.h>
#include <asm/semaphore.h>

#include <linux/diskdumplib.h>

#define QLOGIC_COMPANY_NAME	"QLogic Corporation"

/*
 * Data bit definitions.
 */
#define BIT_0   0x1
#define BIT_1   0x2
#define BIT_2   0x4
#define BIT_3   0x8
#define BIT_4   0x10
#define BIT_5   0x20
#define BIT_6   0x40
#define BIT_7   0x80
#define BIT_8   0x100
#define BIT_9   0x200
#define BIT_10  0x400
#define BIT_11  0x800
#define BIT_12  0x1000
#define BIT_13  0x2000
#define BIT_14  0x4000
#define BIT_15  0x8000
#define BIT_16  0x10000
#define BIT_17  0x20000
#define BIT_18  0x40000
#define BIT_19  0x80000
#define BIT_20  0x100000
#define BIT_21  0x200000
#define BIT_22  0x400000
#define BIT_23  0x800000
#define BIT_24  0x1000000
#define BIT_25  0x2000000
#define BIT_26  0x4000000
#define BIT_27  0x8000000
#define BIT_28  0x10000000
#define BIT_29  0x20000000
#define BIT_30  0x40000000
#define BIT_31  0x80000000

#define LSB(x)	((uint8_t)(x))
#define MSB(x)	((uint8_t)((uint16_t)(x) >> 8))

#define LSW(x)	((uint16_t)(x))
#define MSW(x)	((uint16_t)((uint32_t)(x) >> 16))

#define LSD(x)	((uint32_t)((uint64_t)(x)))
#define MSD(x)	((uint32_t)((((uint64_t)(x)) >> 16) >> 16))

#if !defined(min)	
#define min(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#endif	

#if !defined(max)	
#define max(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })
#endif


/*
 *  Local Macro Definitions.
 */
#if defined(QL_DEBUG_LEVEL_1) || defined(QL_DEBUG_LEVEL_2) || \
    defined(QL_DEBUG_LEVEL_3) || defined(QL_DEBUG_LEVEL_4) || \
    defined(QL_DEBUG_LEVEL_5) || defined(QL_DEBUG_LEVEL_6) || \
    defined(QL_DEBUG_LEVEL_7) || defined(QL_DEBUG_LEVEL_8) || \
    defined(QL_DEBUG_LEVEL_9) || defined(QL_DEBUG_LEVEL_10) || \
    defined(QL_DEBUG_LEVEL_11)
    #define QL_DEBUG_ROUTINES
#endif

#define PERSIST_STRING( s1, s2 ) \
{\
	if( !ql2xdevflag ) \
	 	sprintf(propbuf, (s1) , ha->instance, tgt, dev_no); \
	else \
		sprintf(propbuf, (s2) , ha->instance, tgt, dev_no); \
}

#if !defined(TRUE)
    #define TRUE  1
#endif

#if !defined(FALSE)
    #define FALSE 0
#endif

typedef char BOOL;


/* 
 * Locking
 */
#include <linux/smp.h>
#define cpuid smp_processor_id()

/*
 * I/O register
*/
/* #define MEMORY_MAPPED_IO  */    /* Enable memory mapped I/O */

#if MEMORY_MAPPED_IO
#define RD_REG_BYTE(addr)         readb(addr)
#define RD_REG_WORD(addr)         readw(addr)
#define RD_REG_DWORD(addr)        readl(addr)
#define WRT_REG_BYTE(addr, data)  writeb(data,addr)
#define WRT_REG_WORD(addr, data)  writew(data,addr)
#define WRT_REG_DWORD(addr, data) writel(data,addr)
#else   /* MEMORY_MAPPED_IO */
#define RD_REG_BYTE(addr)         (inb((unsigned long)addr))
#define RD_REG_WORD(addr)         (inw((unsigned long)addr))
#define RD_REG_DWORD(addr)        (inl((unsigned long)addr))
#define WRT_REG_BYTE(addr, data)  (outb(data,(unsigned long)addr))
#define WRT_REG_WORD(addr, data)  (outw(data,(unsigned long)addr))
#define WRT_REG_DWORD(addr, data) (outl(data,(unsigned long)addr))
#endif  /* MEMORY_MAPPED_IO */

/* The ISP2312 v2 chip cannot access the FLASH/GPIO registers via MMIO
 * in 133 MHZ slot.
 */
#define RD_REG_WORD_PIO(addr)         (inw((unsigned long)addr))
#define WRT_REG_WORD_PIO(addr, data)  (outw(data,(unsigned long)addr))

/*
 * Fibre Channel device definitions.
 */
#define WWN_SIZE		8	/* Size of WWPN, WWN & WWNN */
#define MAX_FIBRE_DEVICES   	256
#define MAX_FIBRE_LUNS  	256
#define	MAX_RSCN_COUNT		10
#define	MAX_HOST_COUNT		16

/*
 * Host adapter default definitions.
 */
#define MAX_BUSES            1  /* We only have one bus today */
#define MAX_TARGETS_2100     MAX_FIBRE_DEVICES
#define MAX_TARGETS_2200     MAX_FIBRE_DEVICES
#define MAX_TARGETS          MAX_FIBRE_DEVICES
#define MIN_LUNS             8
#define MAX_LUNS             MAX_FIBRE_LUNS
#define MAX_CMDS_PER_LUN     255 
#define MAX_SRBS             4096


                                    
/*
 * Fibre Channel device definitions.
 */
#if defined(EXTENDED_IDS)
#define SNS_LAST_LOOP_ID	0x7ff
#else
#define SNS_LAST_LOOP_ID	0xfe
#endif

#define LAST_LOCAL_LOOP_ID  0x7d
#define SNS_FL_PORT         0x7e
#define FABRIC_CONTROLLER   0x7f
#define SIMPLE_NAME_SERVER  0x80
#define SNS_FIRST_LOOP_ID   0x81
#define MANAGEMENT_SERVER   0xfe
#define BROADCAST           0xff

#define RESERVED_LOOP_ID(x)	((x > LAST_LOCAL_LOOP_ID && \
				 x < SNS_FIRST_LOOP_ID) || \
				 x == MANAGEMENT_SERVER || \
				 x == BROADCAST)
/*
 * Fibre Channel device definitions.
 */
#define NPH_LAST_HANDLE		0x7ff
						/* Port ID. */
#define NPH_SNS			0x7fc		/*  FFFFFC */
#define NPH_FABRIC_CONTROLLER	0x7fd		/*  FFFFFD */
#define NPH_F_PORT		0x7fe		/*  FFFFFE */
#define NPH_IP_BROADCAST	0x7ff		/*  FFFFFF */

#define RESERVED_LOOP_ID_24XX(x)	((x > LAST_LOCAL_LOOP_ID && \
					 x < NPH_LAST_HANDLE) || \
					 x == NPH_SNS || \
					 x == NPH_FABRIC_CONTROLLER || \
					 x == NPH_F_PORT || \
					 x == NPH_IP_BROADCAST)

#define SNS_ACCEPT          0x0280      /* 8002 swapped */
#define SNS_REJECT          0x0180      /* 8001 swapped */

/* Loop ID's used as database flags, must be higher than any valid Loop ID */
#define PORT_UNUSED         0x100       /* Port never been used. */
#define PORT_AVAILABLE      0x101       /* Device does not exist on port. */
#define PORT_NEED_MAP       0x102       
#define PORT_LOST_ID        0x200       
#define PORT_LOGIN_NEEDED   0x400       

// Inbound or Outbound tranfer of data
#define QLA2X00_UNKNOWN  0
#define QLA2X00_READ	1
#define QLA2X00_WRITE	2

/*
 * Timeout timer counts in seconds
 */
#define QLA2100_WDG_TIME_QUANTUM   5    /* In seconds */
#define PORT_RETRY_TIME            2
#define LOOP_DOWN_TIMEOUT          60
#define LOOP_DOWN_TIME             255    /* 255 */
#define	LOOP_DOWN_RESET		(LOOP_DOWN_TIME - 30)

/* Maximum outstanding commands in ISP queues (1-65535) */
#define MAX_OUTSTANDING_COMMANDS   2048

/* ISP request and response entry counts (37-65535) */
#define REQUEST_ENTRY_CNT       512     /* Number of request entries. */
#if defined(ISP2100) || defined(ISP2200)
#define RESPONSE_ENTRY_CNT      64      /* Number of response entries.*/
#endif
#if defined(ISP2300)
#define RESPONSE_ENTRY_CNT      512     /* Number of response entries.*/
#endif
/* #define REQUEST_ENTRY_CNT_24XX  4096 */    /* Number of request entries.*/
#define REQUEST_ENTRY_CNT_24XX  REQUEST_ENTRY_CNT     /* Number of request entries.*/

#define  SCSI_BUS_32(scp)   ((scp)->channel)
#define  SCSI_TCN_32(scp)    ((scp)->target)
#define  SCSI_LUN_32(scp)    ((scp)->lun)

/* PCI Definitions */

#if  !defined(PCI_CAP_ID_PCIX)
#define  PCI_CAP_ID_PCIX        0x07    /* PCI-X */
#endif


#if  !defined( PCI_X_CMD)
#define PCI_X_CMD               2       /* Modes & Features */
#endif


#if !defined(PCI_X_CMD_MAX_READ)
#define  PCI_X_CMD_MAX_READ     0x000c  /* Max Memory Read Byte Count */
#endif

#if !defined(PCI_CAP_ID_EXP)
#define  PCI_CAP_ID_EXP         0x10    /* PCI-EXPRESS */     
#endif
/*
 * UnixWare required definitions.
 */
#define HBA_PREFIX qla2100

/* Physical DMA memory requirements */
#define QLA2100_MEMALIGN    4
#define QLA2100_BOUNDARY    0x80000000  /* 2GB */

/* Calculate the number of SG segments */
#define SGDATA_PER_REQUEST	2 
#define SGDATA_PER_CONT		7

/*
 * SCSI Request Block 
 */
typedef struct srb
{
    struct list_head   list;
    Scsi_Cmnd  *cmd;                 /* Linux SCSI command pkt */
    struct scsi_qla_host *ha;		/* ha this SP is queued on */
    uint8_t     more_cdb[4];         /* For 16 bytes CDB pass thru cmd since
                                        linux SCSI cdb is 12 bytes. */ 
    uint8_t     dir;                 /* direction of transfer */
    uint8_t     unused1;
    uint8_t     ccode;               /* risc completion code */
    uint8_t     scode;               /* scsi status code */
    
    uint16_t    flags;               /* Status flags - defined below */
    uint16_t     state;
#define SRB_FREE_STATE          0    /* Request returned back */
#define SRB_PENDING_STATE       1    /* Request being queued in LUN Q */
#define SRB_ACTIVE_STATE        2    /* Request in Active Array */
#define SRB_DONE_STATE          3    /* Request Queued in Done Queue */
#define SRB_RETRY_STATE         4    /* Request in Retry Queue */
#define SRB_SUSPENDED_STATE     5    /* Request in suspended state */
#define SRB_NO_QUEUE_STATE      6    /* Request is in between states */
#define SRB_ACTIVE_TIMEOUT_STATE 7   /* Request in Active Array but timed out */
#define SRB_FAILOVER_STATE 	8    /* Request in Failover Queue */
#define SRB_SCSI_RETRY_STATE    9    /* Request in Scsi Retry Queue */

    uint8_t     used;		     /* used by allocation code */
    uint8_t     ref_num;             /* reference SRB number */	
    uint16_t    magic;               /* qlogic magic number */
#define SRB_MAGIC       0x10CB

    u_long      host_no;             /* Host number of allocating host */
    struct      timer_list   timer;  /* used to timeout command */
    dma_addr_t	 saved_dma_handle;    /* for unmap of single transfers */

    atomic_t	 ref_count;	      /* reference count for this structure */			
	/* Target/LUN queue pointers. */
    struct os_tgt		*tgt_queue;	/* ptr to visible ha's target */
    struct os_lun		*lun_queue;	/* ptr to visible ha's lun */
    struct fc_lun		*fclun;		/* FC LUN context pointer. */
	/* Raw completion info for use by failover ? */
    uint8_t	fo_retry_cnt;	/* Retry count this request */
    uint8_t	err_id;		/* error id */
#define SRB_ERR_PORT       1    /* Request failed because "port down" */
#define SRB_ERR_LOOP       2    /* Request failed because "loop down" */
#define SRB_ERR_DEVICE     3    /* Request failed because "device error" */
#define SRB_ERR_OTHER      4
#define SRB_ERR_RETRY  	   5

    uint8_t	cmd_length;		/* command length */
    uint8_t	qfull_retry_count;

    int      delay;             /* delay in seconds */
    int      ext_history;             /*  */
	
    u_long      e_start;             /* jiffies at start of extend timeout */
    u_long      r_start;             /* jiffies at start of request */
    u_long      u_start;             /* jiffies when sent to F/W    */
    u_long      f_start;            /*ra 10/29/01*/ /*jiffies when put in failover queue*/
    uint32_t    resid;              /* Residual transfer length */
    uint16_t    sense_len;          /* Sense data length */
    uint32_t    request_sense_length;
    void        *request_sense_ptr;

    uint32_t 	 iocb_cnt;
}srb_t;

/*
 * SRB flag definitions
 */
#define SRB_TIMEOUT          BIT_0	/* Command timed out */
#define SRB_DMA_VALID        BIT_1	/* Command sent to ISP */
#define SRB_WATCHDOG         BIT_2	/* Command on watchdog list */
#define SRB_ABORT_PENDING    BIT_3	/* Command abort sent to device */

#define SRB_ABORTED          BIT_4	/* Command aborted command already */
#define SRB_RETRY            BIT_5	/* Command needs retrying */
#define SRB_GOT_SENSE	     BIT_6	/* Command has sense data */
#define SRB_FAILOVER         BIT_7	/* Command in failover state */

#define SRB_BUSY             BIT_8	/* Command is in busy retry state */
#define SRB_FO_CANCEL        BIT_9	/* Command don't need to do failover */
#define	SRB_IOCTL	     BIT_10	/* IOCTL command. */
#define	SRB_ISP_STARTED	     BIT_11	/* Command sent to ISP. */

#define	SRB_ISP_COMPLETED    BIT_12	/* ISP finished with command */
#define	SRB_FDMI_CMD	     BIT_13	/* MSIOCB/non-ioctl command. */
#define	SRB_TAPE		BIT_14	/* TAPE command. */


/*
 *  ISP PCI Configuration Register Set
 */
typedef volatile struct
{
    uint16_t vendor_id;                 /* 0x0 */
    uint16_t device_id;                 /* 0x2 */
    uint16_t command;                   /* 0x4 */
    uint16_t status;                    /* 0x6 */
    uint8_t revision_id;                /* 0x8 */
    uint8_t programming_interface;      /* 0x9 */
    uint8_t sub_class;                  /* 0xa */
    uint8_t base_class;                 /* 0xb */
    uint8_t cache_line;                 /* 0xc */
    uint8_t latency_timer;              /* 0xd */
    uint8_t header_type;                /* 0xe */
    uint8_t bist;                       /* 0xf */
    uint32_t base_port;                 /* 0x10 */
    uint32_t mem_base_addr;             /* 0x14 */
    uint32_t base_addr[4];              /* 0x18-0x24 */
    uint32_t reserved_1[2];             /* 0x28-0x2c */
    uint16_t expansion_rom;             /* 0x30 */
    uint32_t reserved_2[2];             /* 0x34-0x38 */
    uint8_t interrupt_line;             /* 0x3c */
    uint8_t interrupt_pin;              /* 0x3d */
    uint8_t min_grant;                  /* 0x3e */
    uint8_t max_latency;                /* 0x3f */
}config_reg_t __attribute__((packed));


#if defined(ISP2100) || defined(ISP2200)
/*
 *  ISP I/O Register Set structure definitions for ISP2200 and ISP2100.
 */
typedef volatile struct
{
    uint16_t flash_address;             /* Flash BIOS address */
    uint16_t flash_data;                /* Flash BIOS data */
    uint16_t unused_1[1];               /* Gap */
    uint16_t ctrl_status;               /* Control/Status */
					/* Flash upper 64K bank select */
        #define CSR_FLASH_64K_BANK	BIT_3  
					/* Flash BIOS Read/Write enable */
        #define CSR_FLASH_ENABLE	BIT_1  
					/* ISP soft reset */
        #define CSR_ISP_SOFT_RESET	BIT_0   
    uint16_t ictrl;                     /* Interrupt control */
        #define ISP_EN_INT      BIT_15  /* ISP enable interrupts. */
        #define ISP_EN_RISC     BIT_3   /* ISP enable RISC interrupts. */
    uint16_t istatus;                   /* Interrupt status */
        #define RISC_INT        BIT_3   /* RISC interrupt */
    uint16_t semaphore;                 /* Semaphore */
    uint16_t nvram;                     /* NVRAM register. */
        #define NV_DESELECT     0
        #define NV_CLOCK        BIT_0
        #define NV_SELECT       BIT_1
        #define NV_DATA_OUT     BIT_2
        #define NV_DATA_IN      BIT_3
	/* Only applicable to ISP2322/ISP6322 */	
	#define	NV_PR_ENABLE	BIT_13	/* protection register enable */
	#define	NV_WR_ENABLE	BIT_14	/* write enable */

    uint16_t mailbox0;                  /* Mailbox 0 */
    uint16_t mailbox1;                  /* Mailbox 1 */
    uint16_t mailbox2;                  /* Mailbox 2 */
    uint16_t mailbox3;                  /* Mailbox 3 */
    uint16_t mailbox4;                  /* Mailbox 4 */
    uint16_t mailbox5;                  /* Mailbox 5 */
    uint16_t mailbox6;                  /* Mailbox 6 */
    uint16_t mailbox7;                  /* Mailbox 7 */
    uint16_t unused_2[0x3b];	        /* Gap */

    uint16_t fpm_diag_config;
    uint16_t unused_3[0x6];		/* Gap */
    uint16_t pcr;	        	/* Processor Control Register.*/
    uint16_t unused_4[0x5];		/* Gap */
    uint16_t mctr;		        /* Memory Configuration and Timing. */
    uint16_t unused_5[0x3];		/* Gap */

    uint16_t fb_cmd;
    uint16_t unused_6[0x3];		/* Gap */

    uint16_t host_cmd;                  /* Host command and control */
        #define HOST_INT      BIT_7     /* host interrupt bit */

    uint16_t unused_7[5];		/* Gap */
    uint16_t gpiod;			/* GPIO data register */
    uint16_t gpioe;			/* GPIO enable register */

#if defined(ISP2200)
    uint16_t unused_8[8];		/* Gap */
    uint16_t mailbox8;                  /* Mailbox 8 */
    uint16_t mailbox9;                  /* Mailbox 9 */
    uint16_t mailbox10;                 /* Mailbox 10 */
    uint16_t mailbox11;                 /* Mailbox 11 */
    uint16_t mailbox12;                 /* Mailbox 12 */
    uint16_t mailbox13;                 /* Mailbox 13 */
    uint16_t mailbox14;                 /* Mailbox 14 */
    uint16_t mailbox15;                 /* Mailbox 15 */
    uint16_t mailbox16;                 /* Mailbox 16 */
    uint16_t mailbox17;                 /* Mailbox 17 */
    uint16_t mailbox18;                 /* Mailbox 18 */
    uint16_t mailbox19;                 /* Mailbox 19 */
    uint16_t mailbox20;                 /* Mailbox 20 */
    uint16_t mailbox21;                 /* Mailbox 21 */
    uint16_t mailbox22;                 /* Mailbox 22 */
    uint16_t mailbox23;                 /* Mailbox 23 */
#endif
} device_reg_t;

#else
/*
 *  I/O Register Set structure definitions for ISP2300/ISP200.
 */
typedef volatile struct
{
    uint16_t flash_address;             /* Flash BIOS address */
    uint16_t flash_data;                /* Flash BIOS data */
    uint16_t unused_1[1];               /* Gap */
    uint16_t ctrl_status;               /* Control/Status */
					/* Flash upper 64K bank select */
        #define CSR_FLASH_64K_BANK	BIT_3  
					/* Flash BIOS Read/Write enable */
        #define CSR_FLASH_ENABLE	BIT_1  
					/* ISP soft reset */
        #define CSR_ISP_SOFT_RESET	BIT_0   
    uint16_t ictrl;                     /* Interrupt control */
        #define ISP_EN_INT      BIT_15  /* ISP enable interrupts. */
    	#define ISP_EN_RISC     BIT_3   /* ISP enable RISC interrupts. */
    uint16_t istatus;                   /* Interrupt status @0xa*/
        #define RISC_INT        BIT_3   /* RISC interrupt */
    uint16_t semaphore;                 /* Semaphore */
    uint16_t nvram;                     /* NVRAM register. @0xf */
        #define NV_DESELECT     0
        #define NV_CLOCK        BIT_0
        #define NV_SELECT       BIT_1
        #define NV_DATA_OUT     BIT_2
        #define NV_DATA_IN      BIT_3
	/* Only applicable to ISP2322/ISP6322 */	
	#define	NV_PR_ENABLE	BIT_13	/* protection register enable */
	#define	NV_WR_ENABLE	BIT_14	/* write enable */
        #define NV_BUSY         BIT_15
    uint16_t req_q_in;                  /* @0x10 */
    uint16_t req_q_out;                 /* @0x12 */
    uint16_t rsp_q_in;                  /* @0x14 */
    uint16_t rsp_q_out;                 /* @0x16 */ 
    uint16_t host_status_lo;            /* RISC to Host Status Low */
        #define HOST_STATUS_INT   BIT_15  /* RISC int */
        #define ROM_MB_CMD_COMP   0x01  /* ROM mailbox cmd complete */
        #define ROM_MB_CMD_ERROR  0x02  /*ROM mailbox cmd unsuccessful*/
        #define MB_CMD_COMP       0x10  /* Mailbox cmd complete */
        #define MB_CMD_ERROR      0x11  /* Mailbox cmd unsuccessful */
        #define ASYNC_EVENT       0x12  /* Asynchronous event */
        #define RESPONSE_QUEUE_INT 0x13 /* Response Queue update */
        #define RIO_ONE           0x15  /* RIO one 16 bit handle */
        #define FAST_SCSI_COMP    0x16  /* Fast Post SCSI complete */
    uint16_t host_status_hi;            /* RISC to Host Status High */
    uint16_t host_semaphore;            /* Host to Host Semaphore */
    uint16_t unused_2[0x11];            /* Gap */
    uint16_t mailbox0;                  /* Mailbox 0 @0x40 */
    uint16_t mailbox1;                  /* Mailbox 1 */
    uint16_t mailbox2;                  /* Mailbox 2 */
    uint16_t mailbox3;                  /* Mailbox 3 */
    uint16_t mailbox4;                  /* Mailbox 4 */
    uint16_t mailbox5;                  /* Mailbox 5 */
    uint16_t mailbox6;                  /* Mailbox 6 */
    uint16_t mailbox7;                  /* Mailbox 7 @0x4E */
    uint16_t mailbox8;                  /* Mailbox 8 */
    uint16_t mailbox9;                  /* Mailbox 9 */
    uint16_t mailbox10;                 /* Mailbox 10 */
    uint16_t mailbox11;                 /* Mailbox 11 */
    uint16_t mailbox12;                 /* Mailbox 12 */
    uint16_t mailbox13;                 /* Mailbox 13 */
    uint16_t mailbox14;                 /* Mailbox 14 */
    uint16_t mailbox15;                 /* Mailbox 15 */
    uint16_t mailbox16;                 /* Mailbox 16 */
    uint16_t mailbox17;                 /* Mailbox 17 */
    uint16_t mailbox18;                 /* Mailbox 18 */
    uint16_t mailbox19;                 /* Mailbox 19 */
    uint16_t mailbox20;                 /* Mailbox 20 */
    uint16_t mailbox21;                 /* Mailbox 21 */
    uint16_t mailbox22;                 /* Mailbox 22 */
    uint16_t mailbox23;                 /* Mailbox 23 */
    uint16_t mailbox24;                  /* Mailbox 24 */
    uint16_t mailbox25;                  /* Mailbox 25 */
    uint16_t mailbox26;                 /* Mailbox 26 */
    uint16_t mailbox27;                 /* Mailbox 27 */
    uint16_t mailbox28;                 /* Mailbox 28 */
    uint16_t mailbox29;                 /* Mailbox 29 */
    uint16_t mailbox30;                 /* Mailbox 30 */
    uint16_t mailbox31;                 /* Mailbox 31 @0x7E */
    uint16_t unused4[0xb];              /* gap */

    uint16_t fpm_diag_config;
    uint16_t unused_3[0x6];		/* Gap */
    uint16_t pcr;	   	        /* Processor Control Register.*/
    uint16_t unused_4[0x5];		/* Gap */
    uint16_t mctr;		        /* Memory Configuration and Timing. */
    uint16_t unused_5[0x3];		/* Gap */
    uint16_t fb_cmd;
    uint16_t unused_6[0x3];		/* Gap */
    uint16_t host_cmd;                  /* Host command and control */
        #define HOST_INT      BIT_7     /* host interrupt bit */

    uint16_t unused_7[5];		/* Gap */
    uint16_t gpiod;			/* GPIO data register */
    uint16_t gpioe;			/* GPIO enable register */
}device_reg_t;

/*
 * ISP I/O Register Set structure definitions.
 */
struct device_reg_24xx {
	volatile uint32_t flash_addr;	/* Flash/NVRAM BIOS address. */
#define FARX_DATA_FLAG	BIT_31
#define FARX_ACCESS_FLASH_CONF	0x7FFD0000
#define FARX_ACCESS_FLASH_DATA	0x7FF00000
#define FARX_ACCESS_NVRAM_CONF	0x7FFF0000
#define FARX_ACCESS_NVRAM_DATA	0x7FFE0000
#define FA_NVRAM_FUNC0_ADDR     0x80
#define FA_NVRAM_FUNC1_ADDR     0x180

#define FA_NVRAM_VPD_SIZE	0x80
#define FA_NVRAM_VPD0_ADDR	0x00
#define FA_NVRAM_VPD1_ADDR	0x100

                                        /*
                                         * RISC code begins at offset 512KB
                                         * within flash. Consisting of two
                                         * contiguous RISC code segments.
                                         */
#define FA_RISC_CODE_ADDR       0x20000
#define FA_RISC_CODE_SEGMENTS   2

	volatile uint32_t flash_data;	/* Flash/NVRAM BIOS data. */

	volatile uint32_t ctrl_status;	/* Control/Status. */
#define CSRX_FLASH_ACCESS_ERROR	BIT_18	/* Flash/NVRAM Access Error. */
#define CSRX_DMA_ACTIVE		BIT_17	/* DMA Active status. */
#define CSRX_DMA_SHUTDOWN	BIT_16	/* DMA Shutdown control status. */
#define CSRX_FUNCTION		BIT_15	/* Function number. */
					/* PCI-X Bus Mode. */
#define CSRX_PCIX_BUS_MODE_MASK	(BIT_11|BIT_10|BIT_9|BIT_8)
#define PBM_PCI_33MHZ		(0 << 8)
#define PBM_PCIX_M1_66MHZ	(1 << 8)
#define PBM_PCIX_M1_100MHZ	(2 << 8)
#define PBM_PCIX_M1_133MHZ	(3 << 8)
#define PBM_PCIX_M2_66MHZ	(5 << 8)
#define PBM_PCIX_M2_100MHZ	(6 << 8)
#define PBM_PCIX_M2_133MHZ	(7 << 8)
#define PBM_PCI_66MHZ		(8 << 8)
					/* Max Write Burst byte count. */
#define CSRX_MAX_WRT_BURST_MASK	(BIT_5|BIT_4)
#define MWB_512_BYTES		(0 << 4)
#define MWB_1024_BYTES		(1 << 4)
#define MWB_2048_BYTES		(2 << 4)
#define MWB_4096_BYTES		(3 << 4)

#define CSRX_64BIT_SLOT		BIT_2	/* PCI 64-Bit Bus Slot. */
#define CSRX_FLASH_ENABLE	BIT_1	/* Flash BIOS Read/Write enable. */
#define CSRX_ISP_SOFT_RESET	BIT_0	/* ISP soft reset. */

	volatile uint32_t ictrl;	/* Interrupt control. */
#define ICRX_EN_RISC_INT	BIT_3	/* Enable RISC interrupts on PCI. */

	volatile uint32_t istatus;	/* Interrupt status. */
#define ISRX_RISC_INT		BIT_3	/* RISC interrupt. */

	uint32_t unused_1[2];		/* Gap. */

					/* Request Queue. */
	volatile uint32_t req_q_in;	/*  In-Pointer. */
	volatile uint32_t req_q_out;	/*  Out-Pointer. */
					/* Response Queue. */
	volatile uint32_t rsp_q_in;	/*  In-Pointer. */
	volatile uint32_t rsp_q_out;	/*  Out-Pointer. */
					/* Priority Request Queue. */
	volatile uint32_t preq_q_in;	/*  In-Pointer. */
	volatile uint32_t preq_q_out;	/*  Out-Pointer. */

	uint32_t unused_2[2];		/* Gap. */

					/* ATIO Queue. */
	volatile uint32_t atio_q_in;	/*  In-Pointer. */
	volatile uint32_t atio_q_out;	/*  Out-Pointer. */

	volatile uint32_t host_status;	
#define HSRX_RISC_INT		BIT_15	/* RISC to Host interrupt. */
#define HSRX_RISC_PAUSED	BIT_8	/* RISC Paused. */

	volatile uint32_t hccr;		/* Host command & control register. */
					/* HCCR statuses. */
#define HCCRX_HOST_INT		BIT_6	/* Host to RISC interrupt bit. */
#define HCCRX_RISC_RESET	BIT_5	/* RISC Reset mode bit. */
#define HCCRX_RISC_PAUSE	BIT_4	/* RISC Pause mode bit. */
					/* HCCR commands. */
					/* NOOP. */
#define HCCRX_NOOP		0x00000000	
					/* Set RISC Reset. */
#define HCCRX_SET_RISC_RESET	0x10000000	
					/* Clear RISC Reset. */
#define HCCRX_CLR_RISC_RESET	0x20000000	
					/* Set RISC Pause. */
#define HCCRX_SET_RISC_PAUSE	0x30000000	
					/* Releases RISC Pause. */
#define HCCRX_REL_RISC_PAUSE	0x40000000	
					/* Set HOST to RISC interrupt. */
#define HCCRX_SET_HOST_INT	0x50000000	
					/* Clear HOST to RISC interrupt. */
#define HCCRX_CLR_HOST_INT	0x60000000	
					/* Clear RISC to PCI interrupt. */
#define HCCRX_CLR_RISC_INT	0xA0000000	

	volatile uint32_t gpiod;	/* GPIO Data register. */
					/* LED update mask. */
#define GPDX_LED_UPDATE_MASK	(BIT_20|BIT_19|BIT_18)
					/* Data update mask. */
#define GPDX_DATA_UPDATE_MASK	(BIT_17|BIT_16)
					/* LED control mask. */
#define GPDX_LED_COLOR_MASK	(BIT_4|BIT_3|BIT_2)
					/* LED bit values. Color names are
					 * as referenced in fw spec.
					 */
#define GPDX_LED_YELLOW_ON	BIT_2
#define GPDX_LED_GREEN_ON	BIT_3
#define GPDX_LED_AMBER_ON	BIT_4
					/* Data in/out. */
#define GPDX_DATA_INOUT		(BIT_1|BIT_0)

	volatile uint32_t gpioe;	/* GPIO Enable register. */
					/* Enable update mask. */
#define GPEX_ENABLE_UPDATE_MASK	(BIT_17|BIT_16)
					/* Enable. */
#define GPEX_ENABLE		(BIT_1|BIT_0)

	volatile uint32_t iobase_addr;	/* I/O Bus Base Address register. */

	uint32_t unused_3[10];		/* Gap. */

	volatile uint16_t mailbox0;
	volatile uint16_t mailbox1;
	volatile uint16_t mailbox2;
	volatile uint16_t mailbox3;
	volatile uint16_t mailbox4;
	volatile uint16_t mailbox5;
	volatile uint16_t mailbox6;
	volatile uint16_t mailbox7;
	volatile uint16_t mailbox8;
	volatile uint16_t mailbox9;
	volatile uint16_t mailbox10;
	volatile uint16_t mailbox11;
	volatile uint16_t mailbox12;
	volatile uint16_t mailbox13;
	volatile uint16_t mailbox14;
	volatile uint16_t mailbox15;
	volatile uint16_t mailbox16;
	volatile uint16_t mailbox17;
	volatile uint16_t mailbox18;
	volatile uint16_t mailbox19;
	volatile uint16_t mailbox20;
	volatile uint16_t mailbox21;
	volatile uint16_t mailbox22;
	volatile uint16_t mailbox23;
	volatile uint16_t mailbox24;
	volatile uint16_t mailbox25;
	volatile uint16_t mailbox26;
	volatile uint16_t mailbox27;
	volatile uint16_t mailbox28;
	volatile uint16_t mailbox29;
	volatile uint16_t mailbox30;
	volatile uint16_t mailbox31;
};

#define FW_DUMP_SIZE_24XX       0x2B0000
struct qla24xx_fw_dump {
	uint32_t host_status;
	uint32_t host_reg[32];
	uint32_t shadow_reg[7];
	uint16_t mailbox_reg[32];
	uint32_t xseq_gp_reg[128];
	uint32_t xseq_0_reg[16];
	uint32_t xseq_1_reg[16];
	uint32_t rseq_gp_reg[128];
	uint32_t rseq_0_reg[16];
	uint32_t rseq_1_reg[16];
	uint32_t rseq_2_reg[16];
	uint32_t cmd_dma_reg[16];
	uint32_t req0_dma_reg[15];
	uint32_t resp0_dma_reg[15];
	uint32_t req1_dma_reg[15];
	uint32_t xmt0_dma_reg[32];
	uint32_t xmt1_dma_reg[32];
	uint32_t xmt2_dma_reg[32];
	uint32_t xmt3_dma_reg[32];
	uint32_t xmt4_dma_reg[32];
	uint32_t xmt_data_dma_reg[16];
	uint32_t rcvt0_data_dma_reg[32];
	uint32_t rcvt1_data_dma_reg[32];
	uint32_t risc_gp_reg[128];
	uint32_t lmc_reg[112];
	uint32_t fpm_hdw_reg[192];
	uint32_t fb_hdw_reg[176];
	uint32_t code_ram[0x2000];
	uint32_t ext_mem[1];
};

#endif

#if defined(ISP2100)
#define	MAILBOX_REGISTER_COUNT	8
#elif defined(ISP2200)
#define	MAILBOX_REGISTER_COUNT	24
#elif defined(ISP2300)
#define	MAILBOX_REGISTER_COUNT	32
#endif

typedef struct {
	uint32_t out_mb;	/* outbound from driver */
	uint32_t in_mb;		/* Incoming from RISC */
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	long	buf_size;
	void	*bufp;
	uint32_t tov;
	uint8_t	flags;
#define MBX_DMA_IN	BIT_0
#define	MBX_DMA_OUT	BIT_1
#define IOCTL_CMD	BIT_2
} mbx_cmd_t;

#define	MBX_TOV_SECONDS	30
#define RISC_SADDRESS          0x100000
/*
 *  ISP product identification definitions in mailboxes after reset.
 */
#define PROD_ID_1           0x4953
#define PROD_ID_2           0x0000
#define PROD_ID_2a          0x5020
#define PROD_ID_3           0x2020
#define PROD_ID_4           0x1
#define PROD_ID_4a          0x2

/*
 * ISP host command and control register command definitions
 */
#define HC_RESET_RISC       0x1000      /* Reset RISC */
#define HC_PAUSE_RISC       0x2000      /* Pause RISC */
#define HC_RELEASE_RISC     0x3000      /* Release RISC from reset. */
#define HC_SET_HOST_INT     0x5000      /* Set host interrupt */
#define HC_CLR_HOST_INT     0x6000      /* Clear HOST interrupt */
#define HC_CLR_RISC_INT     0x7000      /* Clear RISC interrupt */
#define HC_RISC_PAUSE       BIT_5
#define	HC_DISABLE_PARITY_PAUSE	0x4001	/* Disable parity error RISC pause. */
#define HC_ENABLE_PARITY    0xA000      /* Enable PARITY interrupt */

/*
 * ISP mailbox Self-Test status codes
 */
#define MBS_FRM_ALIVE       0           /* Firmware Alive. */
#define MBS_CHKSUM_ERR      1           /* Checksum Error. */
#define MBS_BUSY            4           /* Busy. */

/*
 * ISP mailbox command complete status codes
 */
#define MBS_CMD_CMP         		0x4000      /* Command Complete. */
#define MBS_INV_CMD         		0x4001      /* Invalid Command. */
#define MBS_HOST_INF_ERR    		0x4002      /* Host Interface Error. */
#define MBS_TEST_FAILED     		0x4003      /* Test Failed. */
#define MBS_CMD_ERR         		0x4005      /* Command Error. */
#define MBS_CMD_PARAM_ERR   		0x4006      /* Command Parameter Error. */
#define MBS_PORT_ID_USED		0x4007
#define MBS_LOOP_ID_USED		0x4008
#define MBS_ALL_IDS_IN_USE		0x4009 /* For ISP200 if host tries to log into more than 8 targets */
#define MBS_NOT_LOGGED_IN		0x400A
#define MBS_LINK_DOWN_ERROR             0x400B
#define MBS_DIAG_ECHO_TEST_ERROR        0x400C

#define MBS_FATAL_ERROR     0xF000      /* Command Fatal Error. */

#define MBS_FIRMWARE_ALIVE          0x0000 
#define MBS_COMMAND_COMPLETE        0x4000 
#define MBS_INVALID_COMMAND         0x4001 

/* F/W will return mbx0:0x4005 and mbx1:0x16 if
 * HBA tries to log into a target through FL Port
 */ 
#define MBS_SC_TOPOLOGY_ERR	0x16  

/* QLogic subroutine status definitions */
#define QL_STATUS_SUCCESS           0
#define QL_STATUS_ERROR             1
#define QL_STATUS_FATAL_ERROR       2
#define QL_STATUS_RESOURCE_ERROR    3
#define QL_STATUS_LOOP_ID_IN_USE    4
#define QL_STATUS_NO_DATA           5
#define QL_STATUS_TIMEOUT           6
#define QL_STATUS_BUSY		    7
#define QL_STATUS_MBX_CMD_ERR	    8

/*
 * ISP mailbox asynchronous event status codes
 */
#define MBA_ASYNC_EVENT         0x8000  /* Asynchronous event. */
#define MBA_RESET               0x8001  /* Reset Detected. */
#define MBA_SYSTEM_ERR          0x8002  /* System Error. */
#define MBA_REQ_TRANSFER_ERR    0x8003  /* Request Transfer Error. */
#define MBA_RSP_TRANSFER_ERR    0x8004  /* Response Transfer Error. */
#define MBA_ATIOQ_TRASNFER_ERR  0x8005  /* ATIO queue trasfer error */
#define MBA_LIP_OCCURRED        0x8010  /* Loop Initialization Procedure */
                                        /* occurred. */
#define MBA_LOOP_UP             0x8011  /* FC Loop UP. */
#define MBA_LOOP_DOWN           0x8012  /* FC Loop Down. */
#define MBA_LIP_RESET           0x8013  /* LIP reset occurred. */
#define MBA_PORT_UPDATE         0x8014  /* Port Database update. */
#define MBA_SCR_UPDATE          0x8015  /* State Change Registration. */
#define MBA_LOOP_INIT_ERR	0x8017  /* Loop Initialization Errors */
#define MBA_FC_SP_UPDATE	0x801b	/* FC-SP Security Update */
#define MBA_RSCN_UPDATE         MBA_SCR_UPDATE
#define MBA_SCSI_COMPLETION     0x8020  /* SCSI Command Complete. */
#define MBA_CTIO_COMPLETION     0x8021  /* CTIO Complete. */
#define MBA_IP_LOW_WATER_MARK	0x8025	/* IP Low Water Mark received. */
#define MBA_IP_RCV_BUFFER_EMPTY	0x8026	/* IP receive buffer empty queue. */
#define MBA_LINK_MODE_UP        0x8030  /* FC Link Mode UP. */
#define MBA_UPDATE_CONFIG       0x8036  /* FC Update Configuration. */
#define MBA_ZIO_UPDATE          0x8040  /* ZIO-Process response queue */
#define RIO_MBS_CMD_CMP_1_16	0x8031	/* Scsi command complete */
#define RIO_MBS_CMD_CMP_2_16	0x8032	/* Scsi command complete */
#define RIO_MBS_CMD_CMP_3_16	0x8033	/* Scsi command complete */
#define RIO_MBS_CMD_CMP_4_16	0x8034	/* Scsi command complete */
#define RIO_MBS_CMD_CMP_5_16	0x8035	/* Scsi command complete */
#define RIO_RESPONSE_UPDATE	0x8040  /* Scsi command complete but check iocb */
#define MBA_RECEIVE_ERR		0x8048  /* Indicates a Frame was dropped
					   from the recieve FIFO */ 	
#define MBA_LS_RJT_RESPONSE	0x8049	/* LS_RJT was sent in response to
					   a recieve ELS */

/* 24XX additional firmware options */
#define ADD_FO_COUNT                    3
#define ADD_FO1_DISABLE_GPIO_LED_CTRL   BIT_6   /* LED bits */
#define ADD_FO1_ENABLE_PUREX_IOCB       BIT_10

#define ADD_FO2_ENABLE_SEL_CLS2         BIT_5

#define ADD_FO3_NO_ABT_ON_LINK_DOWN     BIT_14

/*
 * ISP mailbox commands
 */
#define MBC_LOAD_RAM              1     /* Load RAM. */
#define MBC_EXECUTE_FIRMWARE      2     /* Execute firmware. */
#define MBC_WRITE_RAM_WORD        4     /* Write RAM word. */
#define MBC_READ_RAM_WORD         5     /* Read RAM word. */
#define MBC_MAILBOX_REGISTER_TEST 6     /* Wrap incoming mailboxes */
#define MBC_VERIFY_CHECKSUM       7     /* Verify checksum. */
#define MBC_GET_FIRMWARE_VERSION  8	/* Get firmware revision. */
#define MBC_LOAD_RAM_A64          9     /* Load RAM by 64-bit address. */
#define MBC_DUMP_RAM              0xA   /* READ BACK FW */
#define MBC_LOAD_RAM_EXTENDED     0xB   /* Load Extended RAM */
#define MBC_DUMP_SRAM             0xC   /* Dump SRAM    */
#define MBC_WRITE_RAM_WORD_EXTENDED   0xD     /* Write RAM word extended */
#define MBC_READ_RAM_EXTENDED     0xF     /* Read RAM extended. */
#define MBC_IOCB_EXECUTE          0x12  /* Execute an IOCB command */
#define MBC_STOP_FIRMWARE	  0x14  /* Stop Firmware */
#define MBC_ABORT_COMMAND         0x15  /* Abort IOCB command. */
#define MBC_ABORT_DEVICE          0x16  /* Abort device (ID/LUN). */
#define MBC_ABORT_TARGET          0x17  /* Abort target (ID). */
#define MBC_TARGET_RESET_ALL      0x18  /* Reset all local targets. */
#define MBC_GET_ADAPTER_LOOP_ID   0x20  /* Get loop id of ISP2100. */
#define MBC_GET_RETRY_COUNT       0x22  /* GET RATOV & retry count */
#define MBC_GET_FIRMWARE_OPTIONS  0x28  /* Get firmware options. */
#define MBC_SET_RETRY_COUNT       0x32  /* SET RATOV & retry count */
#define MBC_SET_FIRMWARE_OPTIONS  0x38  /* Set firmware options. */
#define MBC_GET_RESOURCE_COUNTS   0x42  /* GET Resource counts */
#define MBC_DIAGNOSTIC_ECHO       0x44  /* Perform ECHO diagnostic */
#define MBC_DIAGNOSTIC_LOOP_BACK  0x45  /* Perform LoopBack diagnostic */
#define MBC_ENHANCED_GET_PORT_DATABASE     0x47  /* Get port database. */
#define MBC_IOCB_EXECUTE_A64	  0x54  /* Execute an IOCB command (64bit) */
#define	MBC_SEND_RNID_ELS         0x57	/* Send RNID ELS request */
#define	MBC_SET_RNID_PARAMS       0x59	/* Set RNID parameters */
#define	MBC_GET_RNID_PARAMS       0x5a	/* Get RNID parameters */
#define MBC_INITIALIZE_FIRMWARE   0x60  /* Initialize firmware */
#define MBC_INITIATE_LIP          0x62  /* Initiate Loop Initialization */
                                        /* Procedure */
#define MBC_GET_FCAL_MAP	0x63  /* Get FC/AL position map */
#define MBC_GET_PORT_DATABASE     0x64  /* Get port database. */
#define MBC_TARGET_RESET	  0x66  /* Target reset. */
#define MBC_GET_FIRMWARE_STATE    0x69  /* Get firmware state. */
#define MBC_GET_PORT_NAME         0x6a  /* Get port name. */
#define MBC_GET_LINK_STATUS       0x6b  /* Get link status. */
#define MBC_LIP_RESET             0x6c  /* LIP reset. */
#define MBC_SEND_SNS_COMMAND      0x6e  /* Send Simple Name Server command. */
#define MBC_LOGIN_FABRIC_PORT     0x6f  /* Login fabric port. */
#define MBC_LOGOUT_FABRIC_PORT    0x71  /* Logout fabric port. */
#define MBC_LIP_FULL_LOGIN        0x72  /* Full login LIP. */
#define	MBC_LOGIN_LOOP_PORT       0x74	/* Login Loop Port. */
#define MBC_GET_PORT_LIST         0x75  /* Get port list. */
#define	MBC_INITIALIZE_RECEIVE_QUEUE	0x77	/* Initialize receive queue */
#define	MBC_SEND_FARP_REQ_COMMAND	0x78	/* FARP request. */
#define	MBC_SEND_FARP_REPLY_COMMAND	0x79	/* FARP reply. */
#define	MBC_GET_ID_LIST			0x7C	/* Get Port ID list. */
#define	MBC_SEND_LFA_COMMAND		0x7D	/* Send Loop Fabric Address */
#define	MBC_LUN_RESET			0x7E	/* Send LUN reset */

/*
 * ISP24xx mailbox commands
 */
#define MBC_SERDES_PARAMS	  	0x10	/* Serdes Tx Parameters. */
#define MBC_GET_IOCB_STATUS             0x12    /* Get IOCB status command. */
#define MBC_GET_TIMEOUT_PARAMS          0x22    /* Get FW timeouts. */
#define MBC_GEN_SYSTEM_ERROR            0x2a    /* Generate System Error. */
#define MBC_SET_TIMEOUT_PARAMS          0x32    /* Set FW timeouts. */
#define MBC_HOST_MEMORY_COPY            0x53    /* Host Memory Copy. */
#define MBC_SEND_RNFT_ELS               0x5e    /* Send RNFT ELS request */
#define MBC_GET_LINK_PRIV_STATS         0x6d    /* Get link & private data. */
#define MBC_SET_VENDOR_ID               0x76    /* Set Vendor ID. */

/* Firmware return data sizes */
#define FCAL_MAP_SIZE	128

/* Mailbox bit definitions for out_mb and in_mb */
#define	MBX_31		BIT_31
#define	MBX_30		BIT_30
#define	MBX_29		BIT_29
#define	MBX_28		BIT_28
#define	MBX_27		BIT_27
#define	MBX_26		BIT_26
#define	MBX_25		BIT_25
#define	MBX_24		BIT_24
#define	MBX_23		BIT_23
#define	MBX_22		BIT_22
#define	MBX_21		BIT_21
#define	MBX_20		BIT_20
#define	MBX_19		BIT_19
#define	MBX_18		BIT_18
#define	MBX_17		BIT_17
#define	MBX_16		BIT_16
#define	MBX_15		BIT_15
#define	MBX_14		BIT_14
#define	MBX_13		BIT_13
#define	MBX_12		BIT_12
#define	MBX_11		BIT_11
#define	MBX_10		BIT_10
#define	MBX_9		BIT_9
#define	MBX_8		BIT_8
#define	MBX_7		BIT_7
#define	MBX_6		BIT_6
#define	MBX_5		BIT_5
#define	MBX_4		BIT_4
#define	MBX_3		BIT_3
#define	MBX_2		BIT_2
#define	MBX_1		BIT_1
#define	MBX_0		BIT_0

/*
 * Firmware state codes from get firmware state mailbox command
 */
#define FSTATE_CONFIG_WAIT      0
#define FSTATE_WAIT_AL_PA       1
#define FSTATE_WAIT_LOGIN       2
#define FSTATE_READY            3
#define FSTATE_LOSS_OF_SYNC     4
#define FSTATE_ERROR            5
#define FSTATE_REINIT           6
#define FSTATE_NON_PART         7

#define FSTATE_CONFIG_CORRECT      0
#define FSTATE_P2P_RCV_LIP         1
#define FSTATE_P2P_CHOOSE_LOOP     2
#define FSTATE_P2P_RCV_UNIDEN_LIP  3
#define FSTATE_FATAL_ERROR         4
#define FSTATE_LOOP_BACK_CONN      5

#if defined(ISP2300)
/* GPIO blink defines */
#define LED_GREEN_OFF_AMBER_OFF		0x0000
#define LED_GREEN_ON_AMBER_OFF		0x0040
#define LED_GREEN_OFF_AMBER_ON		0x0080
#define LED_GREEN_ON_AMBER_ON		0x00C0
#define LED_MASK			0x00C1
#define LED_RED_ON_OTHER_OFF		0x0001
#define LED_RGA_ON			0x00C1 /* red, green, amber */
#define DISABLE_GPIO			0x0040 /* Disable GPIO pins */
#endif


/*
 * Port Database structure definition
 * Little endian except where noted.
 */
//#define	PORT_DATABASE_SIZE	128	/* bytes */
#define	PORT_DATABASE_SIZE	sizeof(port_database_t)
typedef struct {
	uint8_t options;
	uint8_t control;
	uint8_t master_state;
	uint8_t slave_state;
#define	PD_STATE_DISCOVERY			0
#define	PD_STATE_WAIT_DISCOVERY_ACK		1
#define	PD_STATE_PORT_LOGIN			2
#define	PD_STATE_WAIT_PORT_LOGIN_ACK		3
#define	PD_STATE_PROCESS_LOGIN			4
#define	PD_STATE_WAIT_PROCESS_LOGIN_ACK		5
#define	PD_STATE_PORT_LOGGED_IN			6
#define	PD_STATE_PORT_UNAVAILABLE		7
#define	PD_STATE_PROCESS_LOGOUT			8
#define	PD_STATE_WAIT_PROCESS_LOGOUT_ACK	9
#define	PD_STATE_PORT_LOGOUT			10
#define	PD_STATE_WAIT_PORT_LOGOUT_ACK		11
	uint8_t reserved[2];
	uint8_t hard_address;
	uint8_t reserved_1;
	uint8_t port_id[4];
	uint8_t node_name[8];			/* Big endian. */
	uint8_t port_name[8];			/* Big endian. */
	uint16_t execution_throttle;
	uint16_t execution_count;
	uint8_t reset_count;
	uint8_t reserved_2;
	uint16_t resource_allocation;
	uint16_t current_allocation;
	uint16_t queue_head;
	uint16_t queue_tail;
	uint16_t transmit_execution_list_next;
	uint16_t transmit_execution_list_previous;
	uint16_t common_features;
	uint16_t total_concurrent_sequences;
	uint16_t RO_by_information_category;
	uint8_t recipient;
	uint8_t initiator;
	uint16_t receive_data_size;
	uint16_t concurrent_sequences;
	uint16_t open_sequences_per_exchange;
	uint16_t lun_abort_flags;
	uint16_t lun_stop_flags;
	uint16_t stop_queue_head;
	uint16_t stop_queue_tail;
	uint16_t port_retry_timer;
	uint16_t next_sequence_id;
	uint16_t frame_count;
	uint16_t PRLI_payload_length;
	uint8_t prli_svc_param_word_0[2];	/* Big endian */
						/* Bits 15-0 of word 0 */
	uint8_t prli_svc_param_word_3[2];	/* Big endian */
						/* Bits 15-0 of word 3 */
	uint16_t loop_id;
	uint16_t extended_lun_info_list_pointer;
	uint16_t extended_lun_stop_list_pointer;
} port_database_t;

/*
 * ISP Initialization Control Block.
 */

struct qla2100_firmware_options
{
#if defined(__BIG_ENDIAN)
        uint8_t unused_15                    :1;
        uint8_t enable_name_change           :1;
        uint8_t enable_full_login_on_lip     :1;
        uint8_t enable_stop_q_on_full        :1;

        uint8_t previous_assigned_addressing :1;
        uint8_t enable_decending_soft_assign :1;
        uint8_t disable_initial_lip          :1;
        uint8_t enable_port_update_event     :1;

        uint8_t enable_lun_response          :1;
        uint8_t enable_adisc                 :1;
        uint8_t disable_initiator_mode       :1;
        uint8_t enable_target_mode           :1;

        uint8_t enable_fast_posting          :1;
        uint8_t enable_full_duplex           :1;
        uint8_t enable_fairness              :1;
        uint8_t enable_hard_loop_id          :1;
#else
        uint8_t enable_hard_loop_id          :1;
        uint8_t enable_fairness              :1;
        uint8_t enable_full_duplex           :1;
        uint8_t enable_fast_posting          :1;

        uint8_t enable_target_mode           :1;
        uint8_t disable_initiator_mode       :1;
        uint8_t enable_adisc                 :1;
        uint8_t enable_lun_response          :1;

        uint8_t enable_port_update_event     :1;
        uint8_t disable_initial_lip          :1;
        uint8_t enable_decending_soft_assign :1;
        uint8_t previous_assigned_addressing :1;

        uint8_t enable_stop_q_on_full        :1;
        uint8_t enable_full_login_on_lip     :1;
        uint8_t enable_name_change           :1;
        uint8_t unused_15                    :1;
#endif
};

struct qla2x00_firmware_options
{
#if defined(__BIG_ENDIAN)
        uint8_t expanded_ifwcb               :1;
        uint8_t node_name_option             :1;
        uint8_t enable_full_login_on_lip     :1;
        uint8_t enable_stop_q_on_full        :1;

        uint8_t previous_assigned_addressing :1;
        uint8_t enable_decending_soft_assign :1;
        uint8_t disable_initial_lip          :1;
        uint8_t enable_port_update_event     :1;

        uint8_t enable_lun_response          :1;
        uint8_t enable_adisc                 :1;
        uint8_t disable_initiator_mode       :1;
        uint8_t enable_target_mode           :1;

        uint8_t enable_fast_posting          :1;
        uint8_t enable_full_duplex           :1;
        uint8_t enable_fairness              :1;
        uint8_t enable_hard_loop_id          :1;
#else
        uint8_t enable_hard_loop_id          :1;
        uint8_t enable_fairness              :1;
        uint8_t enable_full_duplex           :1;
        uint8_t enable_fast_posting          :1;

        uint8_t enable_target_mode           :1;
        uint8_t disable_initiator_mode       :1;
        uint8_t enable_adisc                 :1;
        uint8_t enable_lun_response          :1;

        uint8_t enable_port_update_event     :1;
        uint8_t disable_initial_lip          :1;
        uint8_t enable_decending_soft_assign :1;
        uint8_t previous_assigned_addressing :1;

        uint8_t enable_stop_q_on_full        :1;
        uint8_t enable_full_login_on_lip     :1;
        uint8_t node_name_option             :1;
        uint8_t expanded_ifwcb               :1;
#endif
};

struct qla2x00_additional_firmware_options
{
#if defined(__BIG_ENDIAN)
        uint8_t unused_15                    :1; /* bit 0 */
        uint8_t enable_cmd_q_target_mode     :1; /* bit 1 */
        uint8_t enable_fc_confirm            :1; /* bit 2 */
        uint8_t enable_fc_tape               :1; /* bit 3 */

        uint8_t unused_11                    :1; /* bit 4 */
        uint8_t unused_10                    :1; /* bit 5 */
        uint8_t enable_ack0                  :1; /* bit 6 */
        uint8_t enable_class2                :1; /* bit 7 */

        uint8_t nonpart_if_hard_addr_failed  :1; /* bit 8 */
        uint8_t connection_options           :3; /* bits 9-11 */
                #define LOOP      0
                #define P2P       1
                #define LOOP_P2P  2
                #define P2P_LOOP  3

        uint8_t operation_mode               :4; /* bits 12-15 */
	        #define ZIO_MODE  5

#else
        uint8_t operation_mode               :4; /* bits 0-3 */
	        #define ZIO_MODE  5

        uint8_t connection_options           :3; /* bits 4-6 */
                #define LOOP      0
                #define P2P       1
                #define LOOP_P2P  2
                #define P2P_LOOP  3
        uint8_t nonpart_if_hard_addr_failed  :1; /* bit 7 */

        uint8_t enable_class2                :1; /* bit 8 */
        uint8_t enable_ack0                  :1; /* bit 9 */
        uint8_t unused_10                    :1; /* bit 10 */
        uint8_t unused_11                    :1; /* bit 11 */

        uint8_t enable_fc_tape               :1; /* bit 12 */
        uint8_t enable_fc_confirm            :1; /* bit 13 */
        uint8_t enable_cmd_q_target_mode     :1; /* bit 14 */
        uint8_t unused_15                    :1; /* bit 15 */
#endif
};

struct qla2x00_special_options
{
#if defined(__BIG_ENDIAN)
	uint8_t	data_rate			:2;
	uint8_t	enable_50_ohm_termination	:1;
	uint8_t	unused_12			:1;

	uint8_t	unused_11			:1;
	uint8_t	unused_10			:1;
	uint8_t	unused_9			:1;
	uint8_t	unused_8			:1;

	uint8_t	disable_auto_plogi_local_loop	:1;
	uint8_t	enable_ooo_frame_handling	:1;
	uint8_t	fcp_rsp_payload			:2;

	uint8_t	unused_3			:1;
	uint8_t	unused_2			:1;
	uint8_t	soft_id_only			:1;
	uint8_t	enable_read_xfr_rdy		:1;
#else
	uint8_t	enable_read_xfr_rdy		:1;
	uint8_t	soft_id_only			:1;
	uint8_t	unused_2			:1;
	uint8_t	unused_3			:1;

	uint8_t	fcp_rsp_payload			:2;
	uint8_t	enable_ooo_frame_handling	:1;
	uint8_t	disable_auto_plogi_local_loop	:1;

	uint8_t	unused_8			:1;
	uint8_t	unused_9			:1;
	uint8_t	unused_10			:1;
	uint8_t	unused_11			:1;
	/* used for led scheme */
	uint8_t	unused_12			:1;
	uint8_t	enable_50_ohm_termination	:1;
	uint8_t	data_rate			:2;
#endif
};
#define SO_DATA_RATE_1GB	0
#define SO_DATA_RATE_2GB	1
#define SO_DATA_RATE_AUTO	2

typedef struct
{
    uint8_t  version;
        #define ICB_VERSION 1
    uint8_t  reserved_1;
    struct qla2x00_firmware_options firmware_options;
    uint16_t frame_length;
    uint16_t iocb_allocation;
    uint16_t execution_throttle;
    uint8_t  retry_count;
    uint8_t  retry_delay;
/* TODO: Fix ISP2100 portname/nodename */
//#if defined(ISP2100)
//    uint8_t  node_name[WWN_SIZE];
//#else
    uint8_t  port_name[WWN_SIZE];
////#endif
    uint16_t adapter_hard_loop_id;
    uint8_t  inquiry_data;
    uint8_t  login_timeout;
/* TODO: Fix ISP2100 portname/nodename */
//#if defined(ISP2100)
//    uint8_t  reserved_2[8];
//#else
    uint8_t  node_name[WWN_SIZE];
//#endif
    uint16_t request_q_outpointer;
    uint16_t response_q_inpointer;
    uint16_t request_q_length;
    uint16_t response_q_length;
    uint32_t request_q_address[2];
    uint32_t response_q_address[2];
    uint16_t lun_enables;
    uint8_t  command_resource_count;
    uint8_t  immediate_notify_resource_count;
    uint16_t timeout;
    uint16_t reserved_3;
    struct qla2x00_additional_firmware_options additional_firmware_options;
    uint8_t     response_accum_timer;
    uint8_t     interrupt_delay_timer;
    struct qla2x00_special_options special_options;
    uint16_t    reserved_4[13];
}init_cb_t;

/*
 * ISP Get/Set Target Parameters mailbox command control flags.
 */

/*
 * Get Link Status mailbox command return buffer.
 */
typedef struct
{
	uint32_t	link_fail_cnt;
	uint32_t	loss_sync_cnt;
	uint32_t	loss_sig_cnt;
	uint32_t	prim_seq_err_cnt;
	uint32_t	inval_xmit_word_cnt;
	uint32_t	inval_crc_cnt;
} link_stat_t;

/*
 * NVRAM Command values.
 */
#define NV_START_BIT            BIT_2
#define NV_WRITE_OP             (BIT_26+BIT_24)
#define NV_READ_OP              (BIT_26+BIT_25)
#define NV_ERASE_OP             (BIT_26+BIT_25+BIT_24)
#define NV_MASK_OP              (BIT_26+BIT_25+BIT_24)
#define NV_DELAY_COUNT          10

/*
 * NVRAM host parameter.
 */

struct qla2xxx_host_p
{
#if defined(__BIG_ENDIAN)
        uint8_t unused_15               :1;
        uint8_t unused_14               :1;
        uint8_t unused_13               :1;
        uint8_t enable_database_storage :1;

        uint8_t enable_target_reset     :1;
        uint8_t enable_lip_full_login   :1;
        uint8_t enable_lip_reset        :1;
        uint8_t enable_64bit_addressing :1;

        uint8_t enable_extended_logging :1;
        uint8_t pci_parity_disable      :1;
        uint8_t set_cache_line_size_1   :1;
        uint8_t disable_risc_code_load  :1;

        uint8_t enable_selectable_boot  :1;
        uint8_t disable_luns            :1;
        uint8_t disable_bios            :1;
        uint8_t unused_0                :1;
#else
        uint8_t unused_0                :1;
        uint8_t disable_bios            :1;
        uint8_t disable_luns            :1;
        uint8_t enable_selectable_boot  :1;

        uint8_t disable_risc_code_load  :1;
        uint8_t set_cache_line_size_1   :1;
        uint8_t pci_parity_disable      :1;
        uint8_t enable_extended_logging :1;

        uint8_t enable_64bit_addressing :1;
        uint8_t enable_lip_reset        :1;
        uint8_t enable_lip_full_login   :1;
        uint8_t enable_target_reset     :1;

        uint8_t enable_database_storage :1;
        uint8_t unused_13               :1;
        uint8_t unused_14               :1;
        uint8_t unused_15               :1;
#endif
};

struct qla2x00_seriallink_firmware_options
{
#if defined(__BIG_ENDIAN)
	uint16_t rx_sens_2g	:4;			
	uint16_t tx_sens_2g	:4;
	uint16_t rx_sens_1g	:4;
	uint16_t tx_sens_1g	:4;

        uint16_t unused_15              :1;
        uint16_t unused_14              :1;
        uint16_t unused_13              :1;
        uint16_t unused_12              :1;
        uint16_t unused_11              :1;
	uint16_t output_enable			:1;
	uint16_t output_emphasis_2g		:2;
	uint16_t output_swing_2g		:3;
	uint16_t output_emphasis_1g		:2;
	uint16_t output_swing_1g		:3;
#else
	uint16_t tx_sens_1g	:4;
	uint16_t rx_sens_1g	:4;
	uint16_t tx_sens_2g	:4;
	uint16_t rx_sens_2g	:4;			

	uint16_t output_swing_1g	:3;
	uint16_t output_emphasis_1g	:2;
	uint16_t output_swing_2g	:3;
	uint16_t output_emphasis_2g	:2;
	uint16_t output_enable		:1;
        uint16_t unused_11              :1;
        uint16_t unused_12              :1;
        uint16_t unused_13              :1;
        uint16_t unused_14              :1;
        uint16_t unused_15              :1;
#endif
};

struct qla2x00_hba_features
{
#if defined(__BIG_ENDIAN)
        uint8_t unused_12               :1;
        uint8_t unused_11               :1;
        uint8_t unused_10               :1;
        uint8_t unused_9                :1;

        uint8_t unused_8                :1;
        uint8_t unused_7                :1;
        uint8_t unused_6                :1;
        uint8_t unused_5                :1;

        uint8_t unused_4                :1;
        uint8_t unused_3                :1;
        uint8_t unused_2                :1;
        uint8_t unused_1                :1;

        uint8_t multi_chip_hba          :1;
        uint8_t buffer_plus_module      :1;
        uint8_t risc_ram_parity         :1;
        uint8_t external_gbic           :1;
#else
        uint8_t external_gbic           :1;
        uint8_t risc_ram_parity         :1;
        uint8_t buffer_plus_module      :1;
        uint8_t multi_chip_hba          :1;

        uint8_t unused_1                :1;
        uint8_t unused_2                :1;
        uint8_t unused_3                :1;
        uint8_t unused_4                :1;

        uint8_t unused_5                :1;
        uint8_t unused_6                :1;
        uint8_t unused_7                :1;
        uint8_t unused_8                :1;

        uint8_t unused_9                :1;
        uint8_t unused_10               :1;
        uint8_t unused_11               :1;
        uint8_t unused_12               :1;
#endif
};

#if defined(ISP2300)
/* For future QLA2XXX */
#define NVRAM_MOD_OFFSET        200 /* Model Number offset: 200-215 */
#define BINZERO                 "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"

#endif

#define NVRAM_HW_ID_SIZE        16 /* 16 bytes reserved for hw_id string*/
#define NVRAM_MODEL_SIZE        16 /* 16 bytes reserved for model_num string*/

#if !defined(ISP2100)

/*
 *  ISP2[23]XX NVRAM structure definitions.
 */
typedef struct
{
    uint8_t     id[4];
    uint8_t     nvram_version;
    uint8_t     reserved_0;

	/*========Start of ICB block ====================*/
    /*
     * NVRAM RISC parameter block
     */

    uint8_t     parameter_block_version;
    uint8_t     reserved_1;

	/* Firmware options */
    struct qla2x00_firmware_options firmware_options;

    uint16_t    frame_payload_size;
    uint16_t    max_iocb_allocation;
    uint16_t    execution_throttle;
    uint8_t     retry_count;
    uint8_t     retry_delay;
    uint8_t     port_name[WWN_SIZE];			/* 0 .. 7 */
    uint16_t    adapter_hard_loop_id;
    uint8_t     inquiry_data;
    uint8_t     login_timeout;

    uint8_t     node_name[WWN_SIZE];			/* 0 .. 7 */

    /* Expanded RISC parameter block */

	/* extended_parameter_options 16bits */
    struct qla2x00_additional_firmware_options additional_firmware_options;

    uint8_t     response_accum_timer;
    uint8_t     interrupt_delay_timer;

	/* special options */
    struct qla2x00_special_options special_options;

    uint16_t    reserved_2[11];
	/*========End of ICB block ====================*/

	/*
	 * Serial Link Control for output Swing, Emphasis and Sensitivity
	 */
    struct qla2x00_seriallink_firmware_options	serial_options;

    /*
     * NVRAM host parameter block
     */

    struct qla2xxx_host_p host_p;

    uint8_t     boot_node_name[WWN_SIZE];
    uint8_t     boot_lun_number;
    uint8_t     reset_delay;
    uint8_t     port_down_retry_count;
    uint8_t     reserved_3;

    uint16_t    maximum_luns_per_target;

    uint16_t    reserved_6[7];

    /* Offset 100 */
    uint8_t    reserved_7_1[10];

    /* Offset 110 */
    /*
     * BIT 0 = Selective Login
     * BIT 1 = Alt-Boot Enable
     * BIT 2 =
     * BIT 3 = Boot Order List
     * BIT 4 =
     * BIT 5 = Selective LUN
     * BIT 6 =
     * BIT 7 = unused
     */
    uint8_t efi_parameters;	

    /* Offset 111 */
    uint8_t    link_down_timeout;		

    /* Offset 112 */
    uint8_t    hw_id[16];

    /* Offset 128 */
    uint8_t    reserved_7_2[22];

    /* Offset 150 */
    uint16_t    reserved_8[25];

    /* Offset 200-215 : Model Number */
    uint8_t    model_number[16];

    /* oem related items */
    uint8_t oem_fru[8];
    uint8_t oem_ec[8];

    /* Offset 232 */
    struct qla2x00_hba_features hba_features;

    uint16_t   reserved_9;
    uint16_t   reserved_10;
    uint16_t   reserved_11;

    uint16_t   reserved_12;
    uint16_t   reserved_13;

    /* Subsystem ID must be at offset 244 */
    uint16_t    subsystem_vendor_id;

    uint16_t    reserved_14;

    /* Subsystem device ID must be at offset 248 */
    uint16_t    subsystem_device_id;

    uint16_t    reserved_15[2];
    uint8_t     reserved_16;
    uint8_t     checksum;
}nvram22_t;

#else

/*
 *  ISP2100 NVRAM structure definitions.
 */
typedef struct
{
    uint8_t     id[4];
    uint8_t     nvram_version;
    uint8_t     reserved_0;

    /*
     * NVRAM RISC parameter block
     */

    uint8_t     parameter_block_version;
    uint8_t     reserved_1;

    struct qla2100_firmware_options firmware_options;

    uint16_t    frame_payload_size;
    uint16_t    max_iocb_allocation;
    uint16_t    execution_throttle;
    uint8_t     retry_count;
    uint8_t     retry_delay;
    uint8_t     node_name[WWN_SIZE];
    uint16_t    adapter_hard_loop_id;
    uint8_t     reserved_2;
    uint8_t     login_timeout;
    uint16_t    reserved_3[4];

    /* Reserved for expanded RISC parameter block */
    uint16_t    reserved_4[16];

    /*
     * NVRAM host parameter block
     */

    struct qla2xxx_host_p host_p;

    uint8_t     boot_node_name[WWN_SIZE];
    uint8_t     boot_lun_number;
    uint8_t     reset_delay;
    uint8_t     port_down_retry_count;
    uint8_t     reserved_5;

    uint16_t    maximum_luns_per_target;

    uint16_t    reserved_6[7];

    /* Offset 100 */
    uint16_t    reserved_7[25];

    /* Offset 150 */
    uint16_t    reserved_8[25];

    /* Offset 200 */
    uint16_t    reserved_9[22];

    /* Subsystem ID must be at offset 244 */
    uint16_t    subsystem_vendor_id;

    uint16_t    reserved_10;

    /* Subsystem device ID must be at offset 248 */
    uint16_t    subsystem_device_id;

    uint16_t    reserved_11[2];
    uint8_t     reserved_12;
    uint8_t     checksum;
}nvram21_t;

#endif

/*
 * ISP queue - command entry structure definition.
 */
#define MAX_CMDSZ   16                  /* SCSI maximum CDB size. */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define COMMAND_TYPE    0x11    /* Command entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t handle;                    /* System handle. */
#if defined(EXTENDED_IDS)
    uint16_t  target;                    /* SCSI ID */
#else
    uint8_t  reserved;
    uint8_t  target;                    /* SCSI ID */
#endif
    uint16_t lun;                       /* SCSI LUN */
    uint16_t control_flags;             /* Control flags. */
#define CF_HEAD_TAG		BIT_1
#define CF_ORDERED_TAG		BIT_2
#define CF_SIMPLE_TAG		BIT_3
#define CF_READ			BIT_5
#define CF_WRITE		BIT_6
    uint16_t reserved_1;
    uint16_t timeout;                   /* Command timeout. */
    uint16_t dseg_count;                /* Data segment count. */
    uint8_t  scsi_cdb[MAX_CMDSZ];       /* SCSI command words. */
    uint32_t byte_count;                /* Total byte count. */
    uint32_t dseg_0_address;            /* Data segment 0 address. */
    uint32_t dseg_0_length;             /* Data segment 0 length. */
    uint32_t dseg_1_address;            /* Data segment 1 address. */
    uint32_t dseg_1_length;             /* Data segment 1 length. */
    uint32_t dseg_2_address;            /* Data segment 2 address. */
    uint32_t dseg_2_length;             /* Data segment 2 length. */
}cmd_entry_t;

/*
 * ISP queue - 64-Bit addressing, command entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define COMMAND_A64_TYPE 0x19   /* Command A64 entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t handle;                    /* System handle. */
#if defined(EXTENDED_IDS)
    uint16_t  target;                    /* SCSI ID */
#else
    uint8_t  reserved;
    uint8_t  target;                    /* SCSI ID */
#endif
    uint16_t lun;                       /* SCSI LUN */
    uint16_t control_flags;             /* Control flags. */
    uint16_t reserved_1;
    uint16_t timeout;                   /* Command timeout. */
    uint16_t dseg_count;                /* Data segment count. */
    uint8_t  scsi_cdb[MAX_CMDSZ];       /* SCSI command words. */
    uint32_t byte_count;                /* Total byte count. */
    uint32_t dseg_0_address[2];         /* Data segment 0 address. */
    uint32_t dseg_0_length;             /* Data segment 0 length. */
    uint32_t dseg_1_address[2];         /* Data segment 1 address. */
    uint32_t dseg_1_length;             /* Data segment 1 length. */
}cmd_a64_entry_t, request_t;

/*
 * ISP queue - continuation entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define CONTINUE_TYPE   0x02    /* Continuation entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t reserved;
    uint32_t dseg_0_address;            /* Data segment 0 address. */
    uint32_t dseg_0_length;             /* Data segment 0 length. */
    uint32_t dseg_1_address;            /* Data segment 1 address. */
    uint32_t dseg_1_length;             /* Data segment 1 length. */
    uint32_t dseg_2_address;            /* Data segment 2 address. */
    uint32_t dseg_2_length;             /* Data segment 2 length. */
    uint32_t dseg_3_address;            /* Data segment 3 address. */
    uint32_t dseg_3_length;             /* Data segment 3 length. */
    uint32_t dseg_4_address;            /* Data segment 4 address. */
    uint32_t dseg_4_length;             /* Data segment 4 length. */
    uint32_t dseg_5_address;            /* Data segment 5 address. */
    uint32_t dseg_5_length;             /* Data segment 5 length. */
    uint32_t dseg_6_address;            /* Data segment 6 address. */
    uint32_t dseg_6_length;             /* Data segment 6 length. */
}cont_entry_t;

/*
 * ISP queue - 64-Bit addressing, continuation entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define CONTINUE_A64_TYPE 0x0A  /* Continuation A64 entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t dseg_0_address[2];         /* Data segment 0 address. */
    uint32_t dseg_0_length;             /* Data segment 0 length. */
    uint32_t dseg_1_address[2];         /* Data segment 1 address. */
    uint32_t dseg_1_length;             /* Data segment 1 length. */
    uint32_t dseg_2_address[2];         /* Data segment 2 address. */
    uint32_t dseg_2_length;             /* Data segment 2 length. */
    uint32_t dseg_3_address[2];         /* Data segment 3 address. */
    uint32_t dseg_3_length;             /* Data segment 3 length. */
    uint32_t dseg_4_address[2];         /* Data segment 4 address. */
    uint32_t dseg_4_length;             /* Data segment 4 length. */
}cont_a64_entry_t;

/*
 * ISP queue - response queue entry definition.
 */
typedef struct
{
	uint8_t data[60];
	uint32_t signature;
}response_t;

#define RESPONSE_PROCESSED 0xDEADDEAD   /* signature */


#define STS_SENSE_BUF_LEN      32
/*
 * ISP queue - status entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define STATUS_TYPE     0x03    /* Status entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t handle;                    /* System handle. */
    uint16_t scsi_status;               /* SCSI status. */
    uint16_t comp_status;               /* Completion status. */
    uint16_t state_flags;               /* State flags. */
    uint16_t status_flags;              /* Status flags. */
    #define ABTS_TERMINATED     BIT_10	/* Transfer terminated by an ABTS */
    #define IOCBSTAT_SF_LOGO	BIT_13	/* logo after 2 abts w/no */
    					/*   response (2 sec) */
    uint16_t rsp_info_len;              /* Response Info Length. */
    uint16_t req_sense_length;          /* Request sense data length. */
    uint32_t residual_length;           /* Residual transfer length. */
    uint8_t  rsp_info[8];               /* FCP response information. */
    uint8_t  req_sense_data[STS_SENSE_BUF_LEN];/* Request sense data. */
}sts_entry_t;

/*
 * Status entry entry status
 */
#define RF_RQ_DMA_ERROR	BIT_6		/* Request Queue DMA error. */
#define RF_INV_E_ORDER	BIT_5		/* Invalid entry order. */
#define RF_INV_E_COUNT	BIT_4		/* Invalid entry count. */
#define RF_INV_E_PARAM	BIT_3		/* Invalid entry parameter. */
#define RF_INV_E_TYPE	BIT_2		/* Invalid entry type. */
#define RF_BUSY		BIT_1		/* Busy */
#define RF_MASK		(RF_RQ_DMA_ERROR | RF_INV_E_ORDER | RF_INV_E_COUNT | \
			 RF_INV_E_PARAM | RF_INV_E_TYPE | RF_BUSY)
#define RF_MASK_24XX	(RF_INV_E_ORDER | RF_INV_E_COUNT | RF_INV_E_PARAM | \
			 RF_INV_E_TYPE)

/*
 * ISP queue - marker entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define MARKER_TYPE     0x04    /* Marker entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
#if defined(EXTENDED_IDS)
    uint16_t  target;                    /* SCSI ID */
#else
    uint8_t  reserved;
    uint8_t  target;                    /* SCSI ID */
#endif
    uint8_t  modifier;                  /* Modifier (7-0). */
        #define MK_SYNC_ID_LUN      0   /* Synchronize ID/LUN */
        #define MK_SYNC_ID          1   /* Synchronize ID */
        #define MK_SYNC_ALL         2   /* Synchronize all ID/LUN */
        #define MK_SYNC_LIP         3   /* Synchronize all ID/LUN, */
                                        /* clear port changed, */
                                        /* use sequence number. */
    uint8_t  reserved_1;
    uint16_t sequence_number;           /* Sequence number of event */
    uint16_t lun;                       /* SCSI LUN */
    uint8_t  reserved_2[48];
}mrk_entry_t;

/*
 * ISP queue - enable LUN entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define ENABLE_LUN_TYPE 0x0B    /* Enable LUN entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
    uint8_t  reserved_8;
    uint8_t  reserved_1;
    uint16_t reserved_2;
    uint32_t reserved_3;
    uint8_t  status;
    uint8_t  reserved_4;
    uint8_t  command_count;             /* Number of ATIOs allocated. */
    uint8_t  immed_notify_count;        /* Number of Immediate Notify */
                                        /* entries allocated. */
    uint16_t reserved_5;
    uint16_t timeout;                   /* 0 = 30 seconds, 0xFFFF = disable */
    uint16_t reserved_6[20];
}elun_entry_t;

/*
 * ISP queue - modify LUN entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define MODIFY_LUN_TYPE 0x0C    /* Modify LUN entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
    uint8_t  reserved_8;
    uint8_t  reserved_1;
    uint8_t  operators;
    uint8_t  reserved_2;
    uint32_t reserved_3;
    uint8_t  status;
    uint8_t  reserved_4;
    uint8_t  command_count;             /* Number of ATIOs allocated. */
    uint8_t  immed_notify_count;        /* Number of Immediate Notify */
                                        /* entries allocated. */
    uint16_t reserved_5;
    uint16_t timeout;                   /* 0 = 30 seconds, 0xFFFF = disable */
    uint16_t reserved_7[20];
}modify_lun_entry_t;

/*
 * ISP queue - immediate notify entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define IMMED_NOTIFY_TYPE 0x0D  /* Immediate notify entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
    uint8_t  reserved_8;
    uint8_t  initiator_id;
#if defined(EXTENDED_IDS)
    uint16_t  target;                    
#else
    uint8_t  reserved_1;
    uint8_t  target_id;
#endif
    uint32_t reserved_2;
    uint16_t status;
    uint16_t task_flags;
    uint16_t seq_id;
    uint16_t reserved_5[11];
    uint16_t scsi_status;
    uint8_t  sense_data[18];
}notify_entry_t;

/*
 * ISP queue - notify acknowledge entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define NOTIFY_ACK_TYPE 0x0E    /* Notify acknowledge entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
    uint8_t  reserved_8;
    uint8_t  initiator_id;
#if defined(EXTENDED_IDS)
    uint16_t  target;                    
#else
    uint8_t  reserved_1;
    uint8_t  target_id;
#endif
    uint16_t flags;
    uint16_t reserved_2;
    uint16_t status;
    uint16_t task_flags;
    uint16_t seq_id;
    uint16_t reserved_3[21];
}nack_entry_t;

/*
 * ISP queue - Accept Target I/O (ATIO) entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define ACCEPT_TGT_IO_TYPE 0x16 /* Accept target I/O entry. */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
#if defined(EXTENDED_IDS)
    uint16_t initiator_id;
#else
    uint8_t  reserved_8;
    uint8_t  initiator_id;
#endif
    uint16_t exchange_id;
    uint16_t flags;
    uint16_t status;
    uint8_t  reserved_1;
    uint8_t  task_codes;
    uint8_t  task_flags;
    uint8_t  execution_codes;
    uint8_t  cdb[MAX_CMDSZ];
    uint32_t data_length;
    uint16_t lun;
    uint16_t reserved_2A;
    uint16_t scsi_status;
    uint8_t  sense_data[18];
}atio_entry_t;

/*
 * ISP queue - Continue Target I/O (CTIO) entry for status mode 0
 *             structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                  /* Entry type. */
        #define CONTINUE_TGT_IO_TYPE 0x17 /* CTIO entry */
    uint8_t  entry_count;                 /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
#if defined(EXTENDED_IDS)
    uint16_t initiator_id;
#else
    uint8_t  reserved_8;
    uint8_t  initiator_id;
#endif
    uint16_t exchange_id;
    uint16_t flags;
    uint16_t status;
    uint16_t timeout;                   /* 0 = 30 seconds, 0xFFFF = disable */
    uint16_t dseg_count;                /* Data segment count. */
    uint32_t relative_offset;
    uint32_t residual;
    uint16_t reserved_1[3];
    uint16_t scsi_status;
    uint32_t transfer_length;
    uint32_t dseg_0_address;            /* Data segment 0 address. */
    uint32_t dseg_0_length;             /* Data segment 0 length. */
    uint32_t dseg_1_address;            /* Data segment 1 address. */
    uint32_t dseg_1_length;             /* Data segment 1 length. */
    uint32_t dseg_2_address;            /* Data segment 2 address. */
    uint32_t dseg_2_length;             /* Data segment 2 length. */
}ctio_entry_t;

/*
 * ISP queue - CTIO returned entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define CTIO_RET_TYPE   0x17    /* CTIO return entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
#if defined(EXTENDED_IDS)
    uint16_t initiator_id;
#else
    uint8_t  reserved_8;
    uint8_t  initiator_id;
#endif
    uint16_t exchange_id;
    uint16_t flags;
    uint16_t status;
    uint16_t timeout;                   /* 0 = 30 seconds, 0xFFFF = disable */
    uint16_t dseg_count;                /* Data segment count. */
    uint32_t relative_offset;
    uint32_t residual;
    uint16_t reserved_1[8];
    uint16_t scsi_status;
    uint8_t  sense_data[18];
}ctio_ret_entry_t;

/*
 * ISP queue - CTIO A64 entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define CTIO_A64_TYPE 0x1F      /* CTIO A64 entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
#if defined(EXTENDED_IDS)
    uint16_t initiator_id;
#else
    uint8_t  reserved_8;
    uint8_t  initiator_id;
#endif
    uint16_t exchange_id;
    uint16_t flags;
    uint16_t status;
    uint16_t timeout;                   /* 0 = 30 seconds, 0xFFFF = disable */
    uint16_t dseg_count;                /* Data segment count. */
    uint32_t relative_offset;
    uint32_t residual;
    uint16_t reserved_1[3];
    uint16_t scsi_status;
    uint32_t transfer_length;
    uint32_t dseg_0_address[2];         /* Data segment 0 address. */
    uint32_t dseg_0_length;             /* Data segment 0 length. */
    uint32_t dseg_1_address[2];         /* Data segment 1 address. */
    uint32_t dseg_1_length;             /* Data segment 1 length. */
}ctio_a64_entry_t;

/*
 * ISP queue - CTIO returned entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define CTIO_A64_RET_TYPE 0x1F  /* CTIO A64 returned entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
#if defined(EXTENDED_IDS)
    uint16_t initiator_id;
#else
    uint8_t  reserved_8;
    uint8_t  initiator_id;
#endif
    uint16_t exchange_id;
    uint16_t flags;
    uint16_t status;
    uint16_t timeout;                   /* 0 = 30 seconds, 0xFFFF = disable */
    uint16_t dseg_count;                /* Data segment count. */
    uint32_t relative_offset;
    uint32_t residual;
    uint16_t reserved_1[8];
    uint16_t scsi_status;
    uint8_t  sense_data[18];
}ctio_a64_ret_entry_t;

/*
 * ISP queue - Status Contination entry structure definition.
 */
#define EXT_STS_SENSE_BUF_LEN      60
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define STATUS_CONT_TYPE 0x10   /* Status contination entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  reserved;
    uint8_t  entry_status;              /* Entry Status. */
    uint8_t  req_sense_data[EXT_STS_SENSE_BUF_LEN];   /* Extended sense data. */
}sts_cont_entry_t;

/*
 * ISP queue - Command Set entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define CMD_SET_TYPE 0x18       /* Command set entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
    uint16_t reserved;
    uint16_t status;
    uint16_t control_flags;             /* Control flags. */
    uint16_t count;
    uint32_t iocb_0_address;
    uint32_t iocb_1_address;
    uint32_t iocb_2_address;
    uint32_t iocb_3_address;
    uint32_t iocb_4_address;
    uint32_t iocb_5_address;
    uint32_t iocb_6_address;
    uint32_t iocb_7_address;
    uint32_t iocb_8_address;
    uint32_t iocb_9_address;
    uint32_t iocb_10_address;
    uint32_t iocb_11_address;
}cmd_set_entry_t;

/*
 * ISP queue - Command Set A64 entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define CMD_SET_TYPE 0x18       /* Command set entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t sys_define_2;              /* System defined. */
    uint16_t reserved;
    uint16_t status;
    uint16_t control_flags;             /* Control flags. */
    uint16_t count;
    uint32_t iocb_0_address[2];
    uint32_t iocb_1_address[2];
    uint32_t iocb_2_address[2];
    uint32_t iocb_3_address[2];
    uint32_t iocb_4_address[2];
    uint32_t iocb_5_address[2];
}cmd_set_a64_entry_t;

/* 4.11
 * ISP queue - Command Set entry structure definition.
 */
typedef struct
{
    uint8_t  entry_type;                /* Entry type. */
        #define MS_IOCB_TYPE 0x29       /*  Management Server IOCB entry */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  sys_define;                /* System defined. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t handle1;                   /* System handle. */
#if defined(EXTENDED_IDS)
    uint16_t loop_id;
#else
    uint8_t  reserved;
    uint8_t  loop_id;
#endif
    uint16_t status;
    uint16_t control_flags;             /* Control flags. */
#define CF_ELS_PASSTHRU		BIT_15
    uint16_t reserved2;
    uint16_t timeout;
    uint16_t cmd_dsd_count;
    uint16_t total_dsd_count;
    uint8_t  type;
    uint8_t  r_ctl;
    uint16_t rx_id;
    uint16_t reserved3;
    uint32_t handle2;
    uint32_t rsp_bytecount;
    uint32_t req_bytecount;
    uint32_t dseg_req_address[2];         /* Data segment 0 address. */
    uint32_t dseg_req_length;             /* Data segment 0 length. */
    uint32_t dseg_rsp_address[2];         /* Data segment 1 address. */
    uint32_t dseg_rsp_length;             /* Data segment 1 length. */
} ms_iocb_entry_t;

/* 4.15
 * RIO Type 1 IOCB response
 */
struct rio_iocb_type1_entry
{
    uint8_t  entry_type;                /* Entry type. */
        #define RIO_IOCB_TYPE1 0x21       /*  IO Completion IOCB */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  handle_count;              /* # of valid handles. */
    uint8_t  entry_status;              /* Entry Status. */
    uint32_t handle[14];		/* handles finished */
}; 

/* 4.16
 * RIO Type 2 IOCB response
 */
struct rio_iocb_type2_entry
{
    uint8_t  entry_type;                /* Entry type. */
        #define RIO_IOCB_TYPE2 0x22       /*  IO Completion IOCB */
    uint8_t  entry_count;               /* Entry count. */
    uint8_t  handle_count;              /* # of valid handles. */
    uint8_t  entry_status;              /* Entry Status. */
    uint16_t handle[29];		/* handles finished */
}; 

/*
 * Port Database structure definition for ISP 24xx.
 */
#define PDO_FORCE_ADISC		BIT_1
#define PDO_FORCE_PLOGI		BIT_0


#define	PORT_DATABASE_24XX_SIZE		sizeof(struct port_database_24xx)	//64
struct port_database_24xx {
	uint16_t flags;
#define PDF_TASK_RETRY_ID	BIT_14
#define PDF_FC_TAPE		BIT_7
#define PDF_ACK0_CAPABLE	BIT_6
#define PDF_FCP2_CONF		BIT_5
#define PDF_CLASS_2		BIT_4
#define PDF_HARD_ADDR		BIT_1

	uint8_t current_login_state;
	uint8_t last_login_state;
#define PDS_PLOGI_PENDING	0x03
#define PDS_PLOGI_COMPLETE	0x04
#define PDS_PRLI_PENDING	0x05
#define PDS_PRLI_COMPLETE	0x06
#define PDS_PORT_UNAVAILABLE	0x07
#define PDS_PRLO_PENDING	0x09
#define PDS_LOGO_PENDING	0x11
//FIXME
#define PDS_PRLI2_PENDING	0x12

	uint8_t hard_address[3];
	uint8_t reserved_1;

	uint8_t port_id[3];
	uint8_t sequence_id;

	uint16_t port_timer;

	uint16_t nport_handle;			/* N_PORT handle. */

	uint16_t receive_data_size;
	uint16_t reserved_2;

	uint8_t prli_svc_param_word_0[2];	/* Big endian */
						/* Bits 15-0 of word 0 */
	uint8_t prli_svc_param_word_3[2];	/* Big endian */
						/* Bits 15-0 of word 3 */

	uint8_t port_name[WWN_SIZE];
	uint8_t node_name[WWN_SIZE];

	uint8_t reserved_3[24];
};

struct nvram_24xx {
	/* NVRAM header. */
	uint8_t id[4];
	uint16_t nvram_version;
	uint16_t reserved_0;

	/* Firmware Initialization Control Block. */
	uint16_t version;
	uint16_t reserved_1;
	uint16_t frame_payload_size;
	uint16_t execution_throttle;
	uint16_t exchange_count;
	uint16_t hard_address;
	uint8_t port_name[WWN_SIZE];
	uint8_t node_name[WWN_SIZE];
	uint16_t login_retry_count;
	uint16_t link_down_on_nos;
	uint16_t interrupt_delay_timer;
	uint16_t login_timeout;
	uint32_t firmware_options_1;
	uint32_t firmware_options_2;
	uint32_t firmware_options_3;

	/* Offset 56. */
	uint16_t seriallink_options[4];	
	uint16_t reserved_2[16];

	/* Offset 96. */
	uint16_t reserved_3[16];

	/* PCIe table entries. */
	uint16_t reserved_4[16];

	/* Offset 160. */
	uint16_t reserved_5[16];

	/* Offset 192. */
	uint16_t reserved_6[16];

	/* Offset 224. */
	uint16_t reserved_7[16];

	/* Host Parameter Block. */
	uint32_t host_p;

	uint8_t alternate_port_name[WWN_SIZE];
	uint8_t alternate_node_name[WWN_SIZE];

	uint8_t boot_port_name[WWN_SIZE];
	uint16_t boot_lun_number;
	uint16_t reserved_8;

	uint8_t alt1_boot_port_name[WWN_SIZE];
	uint16_t alt1_boot_lun_number;
	uint16_t reserved_9;

	uint8_t alt2_boot_port_name[WWN_SIZE];
	uint16_t alt2_boot_lun_number;
	uint16_t reserved_10;

	uint8_t alt3_boot_port_name[WWN_SIZE];
	uint16_t alt3_boot_lun_number;
	uint16_t reserved_11;

	/* EFI Parameter Block. */
	uint32_t efi_parameters;

	uint8_t reset_delay;
	uint8_t reserved_12;
	uint16_t reserved_13;

	uint16_t boot_id_number;
	uint16_t reserved_14;

	uint16_t max_luns_per_target;
	uint16_t reserved_15;

	uint16_t port_down_retry_count;
	uint16_t link_down_timeout;
	
	uint16_t reserved_16[4];

	/* Offset 352. */
	uint8_t prev_drv_ver_major;
	uint8_t prev_drv_ver_submajob;
	uint8_t prev_drv_ver_minor;
	uint8_t prev_drv_ver_subminor;

	uint16_t prev_bios_ver_major;
	uint16_t prev_bios_ver_minor;

	uint16_t prev_efi_ver_major;
	uint16_t prev_efi_ver_minor;

	uint16_t prev_fw_ver_major;
	uint8_t prev_fw_ver_minor;
	uint8_t prev_fw_ver_subminor;

	uint16_t reserved_17[8];

	/* Offset 384. */
	uint16_t reserved_18[16];

	/* Offset 416. */
	uint16_t reserved_19[16];

	/* Offset 448. */
	uint16_t reserved_20[16];

	/* Offset 480. */
	uint8_t model_name[8];

	uint16_t reserved_21[6];

	/* Offset 500. */
	/* HW Parameter Block. */
	uint16_t pcie_table_sig;
	uint16_t pcie_table_offset;

	uint16_t subsystem_vendor_id;
	uint16_t subsystem_device_id;

	uint32_t checksum;
};

/*
 * ISP Initialization Control Block.
 * Little endian except where noted.
 */
#define	ICB_VERSION 1
struct init_cb_24xx {
	uint16_t version;
	uint16_t reserved_1;

	uint16_t frame_payload_size;
	uint16_t execution_throttle;
	uint16_t exchange_count;

	uint16_t hard_address;

	uint8_t port_name[WWN_SIZE];		/* Big endian. */
	uint8_t node_name[WWN_SIZE];		/* Big endian. */

	uint16_t response_q_inpointer;
	uint16_t request_q_outpointer;

	uint16_t login_retry_count;

	uint16_t prio_request_q_outpointer;

	uint16_t response_q_length;
	uint16_t request_q_length;

	uint16_t link_down_timeout;		/* Milliseconds. */

	uint16_t prio_request_q_length;

	uint32_t request_q_address[2];
	uint32_t response_q_address[2];
	uint32_t prio_request_q_address[2];

	uint8_t reserved_2[8];

	uint16_t atio_q_inpointer;
	uint16_t atio_q_length;
	uint32_t atio_q_address[2];

	uint16_t interrupt_delay_timer;		/* 100 microsecond increments. */
	uint16_t login_timeout;

	/*
	 * BIT 0  = Enable Hard Loop Id
	 * BIT 1  = Enable Fairness
	 * BIT 2  = Enable Full-Duplex
	 * BIT 3  = Reserved
	 * BIT 4  = Enable Target Mode
	 * BIT 5  = Disable Initiator Mode
	 * BIT 6  = Reserved
	 * BIT 7  = Reserved
	 *
	 * BIT 8  = Reserved
	 * BIT 9  = Non Participating LIP
	 * BIT 10 = Descending Loop ID Search
	 * BIT 11 = Acquire Loop ID in LIPA
	 * BIT 12 = Reserved
	 * BIT 13 = Full Login after LIP
	 * BIT 14 = Node Name Option
	 * BIT 15-31 = Reserved
	 */
	uint32_t firmware_options_1;

	/*
	 * BIT 0  = Operation Mode bit 0
	 * BIT 1  = Operation Mode bit 1
	 * BIT 2  = Operation Mode bit 2
	 * BIT 3  = Operation Mode bit 3
	 * BIT 4  = Connection Options bit 0
	 * BIT 5  = Connection Options bit 1
	 * BIT 6  = Connection Options bit 2
	 * BIT 7  = Enable Non part on LIHA failure
	 *
	 * BIT 8  = Enable Class 2
	 * BIT 9  = Enable ACK0
	 * BIT 10 = Reserved
	 * BIT 11 = Enable FC-SP Security
	 * BIT 12 = FC Tape Enable
	 * BIT 13-31 = Reserved
	 */
	uint32_t firmware_options_2;

	/*
	 * BIT 0  = Reserved
	 * BIT 1  = Soft ID only
	 * BIT 2  = Reserved
	 * BIT 3  = Reserved
	 * BIT 4  = FCP RSP Payload bit 0
	 * BIT 5  = FCP RSP Payload bit 1
	 * BIT 6  = Enable Receive Out-of-Order data frame handling
	 * BIT 7  = Disable Automatic PLOGI on Local Loop
	 *
	 * BIT 8  = Reserved
	 * BIT 9  = Enable Out-of-Order FCP_XFER_RDY relative offset handling
	 * BIT 10 = Reserved
	 * BIT 11 = Reserved
	 * BIT 12 = Reserved
	 * BIT 13 = Data Rate bit 0
	 * BIT 14 = Data Rate bit 1
	 * BIT 15 = Data Rate bit 2
	 * BIT 16 = Enable 75 ohm termination
	 * BIT 17-31 = Reserved
	 */
	uint32_t firmware_options_3;

	uint8_t  reserved_3[24];
};

struct init_cb {
	union {
		init_cb_t icb;
		struct init_cb_24xx icb24;
	} cb;
};

/*
 * ISP queue - command entry structure definition.
 */
#define COMMAND_TYPE_6	0x48		/* Command Type 6 entry */
struct cmd_type_6 {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */
	uint16_t timeout;		/* Command timeout. */
#define FW_MAX_TIMEOUT		0x1999

	uint16_t dseg_count;		/* Data segment count. */

	uint16_t fcp_rsp_dsd_len;	/* FCP_RSP DSD length. */

	uint8_t lun[8];			/* FCP LUN (BE). */

	uint16_t control_flags;		/* Control flags. */
#define CF_DATA_SEG_DESCR_ENABLE	BIT_2
#define CF_READ_DATA			BIT_1
#define CF_WRITE_DATA			BIT_0

	uint16_t fcp_cmnd_dseg_len;		/* Data segment length. */
	uint32_t fcp_cmnd_dseg_address[2];	/* Data segment address. */

	uint32_t fcp_rsp_dseg_address[2];	/* Data segment address. */

	uint32_t byte_count;		/* Total byte count. */

	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint32_t fcp_data_dseg_address[2];	/* Data segment address. */
	uint16_t fcp_data_dseg_len;		/* Data segment length. */
	uint16_t reserved_1;			/* MUST be set to 0. */
};

#define COMMAND_TYPE_7	0x18		/* Command Type 7 entry */
struct cmd_type_7 {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */
	uint16_t timeout;		/* Command timeout. */

	uint16_t dseg_count;		/* Data segment count. */
	uint16_t reserved_1;

	uint8_t lun[8];			/* FCP LUN (BE). */

	uint16_t task_mgmt_flags;	/* Task management flags. */
#define TMF_CLEAR_ACA		BIT_14
#define TMF_TARGET_RESET	BIT_13
#define TMF_LUN_RESET		BIT_12
#define TMF_CLEAR_TASK_SET	BIT_10
#define TMF_ABORT_TASK_SET	BIT_9
#define TMF_READ_DATA		BIT_1
#define TMF_WRITE_DATA		BIT_0

	uint8_t task;
#define TSK_SIMPLE		0
#define TSK_HEAD_OF_QUEUE	1
#define TSK_ORDERED		2
#define TSK_ACA			4
#define TSK_UNTAGGED		5

	uint8_t crn;

	uint8_t fcp_cdb[MAX_CMDSZ]; 	/* SCSI command words. */
	uint32_t byte_count;		/* Total byte count. */
	
	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_len;		/* Data segment 0 length. */
};

/*
 * ISP queue - status entry structure definition.
 */
#define	STATUS_TYPE	0x03		/* Status entry. */
struct sts_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t comp_status;		/* Completion status. */
	uint16_t ox_id;			/* OX_ID used by the firmware. */

	uint32_t residual_len;		/* Residual transfer length. */

	uint16_t reserved_1;
	uint16_t state_flags;		/* State flags. */
#define SF_TRANSFERRED_DATA	BIT_11
#define SF_FCP_RSP_DMA		BIT_0

	uint16_t reserved_2;
	uint16_t scsi_status;		/* SCSI status. */
#define SS_CONFIRMATION_REQ		BIT_12

	uint32_t rsp_residual_count;	/* FCP RSP residual count. */

	uint32_t sense_len;		/* FCP SENSE length. */
	uint32_t rsp_data_len;		/* FCP response data length. */

	uint8_t data[28];		/* FCP response/sense information. */
};

/*
 * Status entry completion status
 */
#define CS_DATA_REASSEMBLY_ERROR 0x11	/* Data Reassembly Error.. */
#define CS_ABTS_BY_TARGET	0x13	/* Target send ABTS to abort IOCB. */
#define CS_FW_RESOURCE		0x2C	/* Firmware Resource Unavailable. */
#define CS_TASK_MGMT_OVERRUN	0x30	/* Task management overrun (8+). */
#define CS_ABORT_BY_TARGET	0x47	/* Abort By Target. */

/*
 * ISP queue - marker entry structure definition.
 */
#define MARKER_TYPE	0x04		/* Marker entry. */
struct mrk_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */

	uint8_t modifier;		/* Modifier (7-0). */
#define MK_SYNC_ID_LUN	0		/* Synchronize ID/LUN */
#define MK_SYNC_ID	1		/* Synchronize ID */
#define MK_SYNC_ALL	2		/* Synchronize all ID/LUN */
	uint8_t reserved_1;

	uint8_t reserved_2;
	uint8_t vp_index;

	uint16_t reserved_3;

	uint8_t lun[8];			/* FCP LUN (BE). */
	uint8_t reserved_4[40];
};

/*
 * ISP queue - CT Pass-Through entry structure definition.
 */
#define CT_IOCB_TYPE		0x29	/* CT Pass-Through IOCB entry */
struct ct_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System Defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t comp_status;		/* Completion status. */

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t cmd_dsd_count;

	uint8_t vp_index;
	uint8_t reserved_1;

	uint16_t timeout;		/* Command timeout. */
	uint16_t reserved_2;

	uint16_t rsp_dsd_count;

	uint8_t reserved_3[10];

	uint32_t rsp_byte_count;
	uint32_t cmd_byte_count;

	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_len;		/* Data segment 0 length. */
	uint32_t dseg_1_address[2];	/* Data segment 1 address. */
	uint32_t dseg_1_len;		/* Data segment 1 length. */
};

/*
 * ISP queue - ELS Pass-Through entry structure definition.
 */
#define ELS_IOCB_TYPE		0x53	/* ELS Pass-Through IOCB entry */
struct els_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System Defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t reserved_1;

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t tx_dsd_count;

	uint8_t vp_index;
	uint8_t sof_type;
#define EST_SOFI3		(1 << 4)
#define EST_SOFI2		(3 << 4)

	uint32_t rx_xchg_address[2];	/* Receive exchange address. */
	uint16_t rx_dsd_count;

	uint8_t opcode;
	uint8_t reserved_2;

	uint8_t port_id[3];
	uint8_t reserved_3;

	uint16_t reserved_4;

	uint16_t control_flags;		/* Control flags. */
#define ECF_PAYLOAD_DESCR_MASK	(BIT_15|BIT_14|BIT_13)
#define EPD_ELS_COMMAND		(0 << 13)
#define EPD_ELS_ACC		(1 << 13)
#define EPD_ELS_RJT		(2 << 13)
#define EPD_RX_XCHG		(3 << 13)
#define ECF_CLR_PASSTHRU_PEND	BIT_12
#define ECF_INCL_FRAME_HDR	BIT_11

	uint32_t rx_byte_count;
	uint32_t tx_byte_count;

	uint32_t tx_address[2];		/* Data segment 0 address. */
	uint32_t tx_len;		/* Data segment 0 length. */
	uint32_t rx_address[2];		/* Data segment 1 address. */
	uint32_t rx_len;		/* Data segment 1 length. */
};


/*
 * ISP queue - Mailbox Command entry structure definition.
 */
#define MBX_IOCB_TYPE	0x39
struct mbx_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t mbx[28];
};


#define LOGINOUT_PORT_IOCB_TYPE	0x52	/* Login/Logout Port entry. */
struct logio_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t comp_status;		/* Completion status. */
#define CS_LOGIO_ERROR		0x31	/* Login/Logout IOCB error. */

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t control_flags;		/* Control flags. */
					/* Modifiers. */
#define LCF_FCP2_OVERRIDE	BIT_9	/* Set/Reset word 3 of PRLI. */
#define LCF_CLASS_2		BIT_8	/* Enable class 2 during PLOGI. */
#define LCF_FREE_NPORT		BIT_7	/* Release NPORT handle after LOGO. */
#define LCF_EXPL_LOGO		BIT_6	/* Perform an explicit LOGO. */
#define LCF_SKIP_PRLI		BIT_5	/* Skip PRLI after PLOGI. */
#define LCF_IMPL_LOGO_ALL	BIT_5	/* Implicit LOGO to all ports. */
#define LCF_COND_PLOGI		BIT_4	/* PLOGI only if not logged-in. */
#define LCF_IMPL_LOGO		BIT_4	/* Perform an implicit LOGO. */
#define LCF_IMPL_PRLO		BIT_4	/* Perform an implicit PRLO. */
					/* Commands. */
#define LCF_COMMAND_PLOGI	0x00	/* PLOGI. */
#define LCF_COMMAND_PRLI	0x01	/* PRLI. */
#define LCF_COMMAND_PDISC	0x02	/* PDISC. */
#define LCF_COMMAND_ADISC	0x03	/* ADISC. */
#define LCF_COMMAND_LOGO	0x08	/* LOGO. */
#define LCF_COMMAND_PRLO	0x09	/* PRLO. */
#define LCF_COMMAND_TPRLO	0x0A	/* TPRLO. */

	uint8_t vp_index;
	uint8_t reserved_1;

	uint8_t port_id[3];		/* PortID of destination port. */

	uint8_t rsp_size;		/* Response size in 32bit words. */

	uint32_t io_parameter[11];	/* General I/O parameters. */
#define LSC_SCODE_NOLINK	0x01
#define LSC_SCODE_NOIOCB	0x02
#define LSC_SCODE_NOXCB		0x03
#define LSC_SCODE_CMD_FAILED	0x04
#define LSC_SCODE_NOFABRIC	0x05
#define LSC_SCODE_FW_NOT_READY	0x07
#define LSC_SCODE_NOT_LOGGED_IN	0x09
#define LSC_SCODE_NOPCB		0x0A

#define LSC_SCODE_ELS_REJECT	0x18
#define LSC_SCODE_CMD_PARAM_ERR	0x19
#define LSC_SCODE_PORTID_USED	0x1A
#define LSC_SCODE_NPORT_USED	0x1B
#define LSC_SCODE_NONPORT	0x1C
#define LSC_SCODE_LOGGED_IN	0x1D
#define LSC_SCODE_NOFLOGI_ACC	0x1F
};

#define TSK_MGMT_IOCB_TYPE	0x14
struct tsk_mgmt_entry {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t reserved_1;

	uint16_t delay;			/* Activity delay in seconds. */

	uint16_t timeout;		/* Command timeout. */

	uint8_t lun[8];			/* FCP LUN (BE). */

	uint32_t control_flags;		/* Control Flags. */
#define TCF_NOTMCMD_TO_TARGET	BIT_31
#define TCF_LUN_RESET		BIT_4
#define TCF_ABORT_TASK_SET	BIT_3
#define TCF_CLEAR_TASK_SET	BIT_2
#define TCF_TARGET_RESET	BIT_1
#define TCF_CLEAR_ACA		BIT_0

	uint8_t reserved_2[20];

	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint8_t reserved_3[12];
};

#define ABORT_IOCB_TYPE	0x33
struct abort_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */
					/* or Completion status. */

	uint16_t options;		/* Options. */
#define AOF_NO_ABTS		BIT_0	/* Do not send any ABTS. */

	uint32_t handle_to_abort;	/* System handle to abort. */

	uint8_t reserved_1[32];

	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint8_t reserved_2[12];
};
 
/*
 * ISP request and response queue entry sizes
 */
#define RESPONSE_ENTRY_SIZE     (sizeof(response_t))
#define REQUEST_ENTRY_SIZE      (sizeof(request_t))

/*
 * ISP status entry - completion status definitions.
 */
#define CS_COMPLETE         0x0         /* No errors */
#define CS_INCOMPLETE       0x1         /* Incomplete transfer of cmd. */
#define CS_DMA              0x2         /* A DMA direction error. */
#define CS_TRANSPORT        0x3         /* Transport error. */
#define CS_RESET            0x4         /* SCSI bus reset occurred */
#define CS_ABORTED          0x5         /* System aborted command. */
#define CS_TIMEOUT          0x6         /* Timeout error. */
#define CS_DATA_OVERRUN     0x7         /* Data overrun. */
#define CS_DATA_UNDERRUN    0x15        /* Data Underrun. */
#define CS_ABORT_MSG        0xE         /* Target rejected abort msg. */
#define CS_DEV_RESET_MSG    0x12        /* Target rejected dev rst msg. */
#define CS_PORT_UNAVAILABLE 0x28        /* Port unavailable (selection timeout) */
#define CS_PORT_LOGGED_OUT  0x29        /* Port Logged Out */
#define CS_PORT_CONFIG_CHG  0x2A        /* Port Configuration Changed */
#define CS_PORT_BUSY        0x2B        /* Port Busy */
#define CS_BAD_PAYLOAD      0x80        /* Driver defined */
#define CS_UNKNOWN          0x81        /* Driver defined */
#define CS_RETRY            0x82        /* Driver defined */
#define CS_QUEUE_FULL	    0x1c        /* Target queue full*/

/*
 * ISP status entry - SCSI status byte bit definitions.
 */
#define SS_MASK			0xfff /* Mask off reserved bits BIT_12-BIT_15*/
#define SS_RESIDUAL_UNDER       BIT_11
#define SS_RESIDUAL_OVER        BIT_10
#define SS_SENSE_LEN_VALID      BIT_9
#define SS_RESPONSE_INFO_LEN_VALID BIT_8

#define SS_RESERVE_CONFLICT     (BIT_4 | BIT_3)
#define SS_BUSY_CONDITION       BIT_3
#define SS_CONDITION_MET        BIT_2
#define SS_CHECK_CONDITION      BIT_1

/*
 * ISP target entries - Flags bit definitions.
 */
#define OF_RESET            BIT_5       /* Reset LIP flag */
#define OF_DATA_IN          BIT_6       /* Data in to initiator */
                                        /*  (data from target to initiator) */
#define OF_DATA_OUT         BIT_7       /* Data out from initiator */
                                        /*  (data from initiator to target) */
#define OF_NO_DATA          (BIT_7 | BIT_6)
#define OF_INC_RC           BIT_8       /* Increment command resource count */
#define OF_FAST_POST        BIT_9       /* Enable mailbox fast posting. */
#define OF_SSTS             BIT_15      /* Send SCSI status */

/*
 * Target Read/Write buffer structure.
 */
#define TARGET_DATA_OFFSET  4
#define TARGET_DATA_SIZE    0x2000      /* 8K */
#define TARGET_INQ_OFFSET   (TARGET_DATA_OFFSET + TARGET_DATA_SIZE)
#define TARGET_SENSE_SIZE   18
#define TARGET_BUF_SIZE     36

#define TARGET_OFFLINE  BIT_0
/*
 * 24 bit port ID type definition.
 */
typedef union {
	uint32_t	b24  : 24;

	struct {
		uint8_t d_id[3];
		uint8_t rsvd_1;
	}r;

	struct {
		uint8_t al_pa;
		uint8_t area;
		uint8_t domain;
		uint8_t rsvd_1;
	}b;
} port_id_t;

/*
 * Switch info gathering structure.
 */
typedef struct {
	port_id_t d_id;
	uint8_t node_name[WWN_SIZE];
	uint8_t port_name[WWN_SIZE];
} sw_info_t;

/*
 * Inquiry command structure.
 */
#define INQ_SCSI_OPCODE	0x12
#define	INQ_DATA_SIZE	36

typedef struct {
	union {
		cmd_a64_entry_t cmd;
		sts_entry_t rsp;
		struct cmd_type_7 cmd24;
		struct sts_entry_24xx rsp24;
	} p;
	uint8_t inq[INQ_DATA_SIZE];
} inq_cmd_rsp_t;

#define VITAL_PRODUCT_DATA_SIZE 128		/* old value 50 */
#define INQ_EVPD_SET	1
#define INQ_DEV_IDEN_PAGE  0x83  	
#define WWLUN_SIZE	32	
/* code set values */
#define  CODE_SET_BINARY	0x01

/* Association field values */
#define  ASSOCIATION_LOGICAL_DEVICE	0x00	
#define  ASSOCIATION_TARGET_PORT	0x01	
#define  ASSOCIATION_TARGET_DEVICE	0x02	

/* Identifier type values */
#define  TYPE_REL_TGT_PORT	0x04
#define  TYPE_TPG_GROUP		0x05

/* Identifier length */
#define  DEFAULT_IDENT_LEN	4

/* Volume Access Control VPD Page */
#define VOL_ACCESS_CTRL_VPD_PAGE	0xc9
 
/* Volume Preferred Path Priority */
#define	 PREFERRED_PATH_PRIORITY		0x1	
#define	 SECONDARY_PATH_PRIORITY		0x2	
 
/* Volume Ownership VPD Page */
#define VOL_OWNERSHIP_VPD_PAGE		0xe0 
#define VOL_OWNERSHIP_BIT		BIT_6 
#define VOL_OWNERSHIP_BIT_VALID		BIT_7 
 
typedef struct {
	union {
		cmd_a64_entry_t cmd;
		sts_entry_t rsp;
		struct cmd_type_7 cmd24;
		struct sts_entry_24xx rsp24;
	} p;
	uint8_t inq[VITAL_PRODUCT_DATA_SIZE];
} evpd_inq_cmd_rsp_t;

typedef struct {
	union {
		cmd_a64_entry_t cmd;
		sts_entry_t rsp;
		struct cmd_type_7 cmd24;
		struct sts_entry_24xx rsp24;
	} p;
} tur_cmd_rsp_t;

/*
 * Report LUN command structure.
 */
#define RPT_LUN_SCSI_OPCODE	0xA0
#define CHAR_TO_SHORT(a, b)	(uint16_t)((uint8_t)b << 8 | (uint8_t)a)

typedef struct {
	uint32_t	len;
	uint32_t	rsrv;
} rpt_hdr_t;

typedef struct {
	struct {
		uint8_t		b : 6;
		uint8_t		address_method : 2;
	} msb;
	uint8_t		lsb;
	uint8_t		unused[6];
} rpt_lun_t;

typedef struct {
	rpt_hdr_t	hdr;
	rpt_lun_t	lst[MAX_LUNS];
} rpt_lun_lst_t;

typedef struct {
	union {
		cmd_a64_entry_t cmd;
		sts_entry_t rsp;
		struct cmd_type_7 cmd24;
		struct sts_entry_24xx rsp24;
	} p;
	rpt_lun_lst_t list;
} rpt_lun_cmd_rsp_t;

/* We know supports 2 x 2 - 2 target port groups with 2 relative 
*  target ports each. */
/* SCSI Report/Set Target Port Groups command and data definitions */
#define SCSIOP_MAINTENANCE_IN       0xA3
#define SCSIOP_MAINTENANCE_OUT      0xA4

#define SCSISA_TARGET_PORT_GROUPS   0x0A

#define TGT_PORT_GRP_COUNT	2
#define	REL_TGT_PORT_GRP_COUNT	2
typedef struct {
	struct {
		/* indicates the state of corresponding tgt port group */
		uint8_t	asym_acc_state : 4;
		uint8_t	rsvd_1 : 3;
		uint8_t	pref :1;

		uint8_t	supp_acc_state : 4;
		uint8_t	rsvd_2 : 4;
	} state;
	/* identifies the controller */
	uint8_t tgt_port_grp[2]; 
	uint8_t rsvd;
	/* indicates reason for the last fail over operation */
	uint8_t	status_code;
	uint8_t vendor_unique;
	/* no of ports on corresponding controller */
	uint8_t tgt_port_count;
	uint8_t	rel_tgt_port[REL_TGT_PORT_GRP_COUNT][4];
} tgt_port_grp_desc;	

/* Single port per tgt port grp descriptor */

typedef struct {
	struct {
		/* indicates the state of corresponding tgt port group */
		uint8_t	asym_acc_state : 4;
		uint8_t	rsvd_1 : 3;
		uint8_t	pref :1;

		uint8_t	supp_acc_state : 4;
		uint8_t	rsvd_2 : 4;
	} state;
	/* identifies the controller */
	uint8_t tgt_port_grp[2]; 
	uint8_t rsvd;
	/* indicates reason for the last fail over operation */
	uint8_t	status_code;
	uint8_t vendor_unique;
	/* no of ports on corresponding controller */
	uint8_t tgt_port_count;
	/* Single port per controller */
	uint8_t	rel_tgt_port[4];
} tgt_port_grp_desc_0;	

typedef struct {
	uint32_t len;	
	//rename it to descriptor ??
	tgt_port_grp_desc tport_grp[TGT_PORT_GRP_COUNT]; 
} rpt_tport_grp_data_t;   	

typedef struct {
	union {
		cmd_a64_entry_t cmd;
		sts_entry_t rsp;
		struct cmd_type_7 cmd24;
		struct sts_entry_24xx rsp24;
	} p;
	rpt_tport_grp_data_t list;
} rpt_tport_grp_rsp_t;

typedef struct {
	/* indicates the state of corresponding tgt port group */
	uint8_t	asym_acc_state : 4;
	uint8_t	rsvd_1 : 4;
	uint8_t	rsvd_2; 
	/* identifies the controller */
	uint8_t tgt_port_grp[2]; 
} set_tgt_port_grp_desc;	

typedef struct {
	uint32_t rsvd;	
	set_tgt_port_grp_desc descriptor[TGT_PORT_GRP_COUNT];
} set_tport_grp_data_t;   	

typedef struct {
	union {
		cmd_a64_entry_t cmd;
		sts_entry_t rsp;
		struct cmd_type_7 cmd24;
		struct sts_entry_24xx rsp24;
	} p;
	set_tport_grp_data_t list;
} set_tport_grp_rsp_t;


/*
 * SCSI Target Queue structure
 */
typedef struct os_tgt {
	struct os_lun		*olun[MAX_LUNS]; /* LUN context pointer. */
	uint8_t			port_down_retry_count;
	struct scsi_qla_host	*ha;
    	uint32_t		down_timer;

	/* Persistent binding information */
	port_id_t		d_id;
	uint8_t			node_name[WWN_SIZE];
	uint8_t			port_name[WWN_SIZE];
	struct fc_port		*vis_port;

	uint8_t			flags;
#define	TGT_BUSY		BIT_0		/* Reached hi-water mark */
#define	TGT_TAGGED_QUEUE	BIT_1		/* Tagged queuing. */
	atomic_t	q_timer;  	/* suspend timer */
	unsigned long	q_flags;	   /* suspend flags */
#define	TGT_SUSPENDED		1
#define	TGT_RETRY_CMDS		2
} os_tgt_t;

/*
 * SCSI LUN Queue structure
 */
typedef struct os_lun {
	struct fc_lun	*fclun;		/* FC LUN context pointer. */
    	spinlock_t      q_lock;       /* Lun Lock */

	u_long		io_cnt;     /* total xfer count since boot */
	u_long		out_cnt;    /* total outstanding IO count */
	u_long		w_cnt;      /* total writes */
	u_long		r_cnt;      /* total reads */
	u_long		act_time;   /* total active time  */
	u_long		resp_time;  /* total response time (target + f/w) */

	unsigned long	q_flag;
#define LUN_MPIO_RESET_CNTS	1	/* Reset fo_retry_cnt */
#define	LUN_MPIO_BUSY		2	/* Lun is changing paths  */
#define	LUN_SCSI_SCAN_DONE	BIT_3	/* indicates the scsi scan is done */
#define	LUN_EXEC_DELAYED	7	/* Lun execution is delayed */

	u_long		q_timeout;           /* total command timeouts */
	atomic_t	q_timer;  /* suspend timer */
	uint32_t	q_count;	/* current count */
	uint32_t	q_max;		/* maxmum count lun can be suspended */
	uint8_t		q_state;	/* lun State */
#define	LUN_STATE_READY	1	/* indicates the lun is ready for i/o */
#define	LUN_STATE_RUN	2	/* indicates the lun has a timer running */
#define	LUN_STATE_WAIT	3	/* indicates the lun is suspended */
#define	LUN_STATE_TIMEOUT  4	/* indicates the lun has timed out */
	void  *fo_info; 
} os_lun_t;


/* LUN BitMask structure definition, array of 32bit words,
 * 1 bit per lun.  When bit == 1, the lun is masked.
 * Most significant bit of mask[0] is lun 0, bit 24 is lun 7.
 */
typedef struct lun_bit_mask {
	/* Must allocate at least enough bits to accomodate all LUNs */
#if ((MAX_FIBRE_LUNS & 0x7) == 0)
	uint8_t	mask[MAX_FIBRE_LUNS >> 3];
#else
	uint8_t	mask[(MAX_FIBRE_LUNS + 8) >> 3];
#endif
} lun_bit_mask_t;

/*
 * Fibre channel port type.
 */
 typedef enum {
	FCT_UNKNOWN,
	FCT_RSCN,
	FCT_SWITCH,
	FCT_BROADCAST,
	FCT_INITIATOR,
	FCT_TARGET
} fc_port_type_t;

struct fc_lun;
/*
 * Fibre channel port structure.
 */
typedef struct fc_port {
 	struct list_head list;
 	struct list_head fcluns;

	struct scsi_qla_host	*ha;
	struct scsi_qla_host	*vis_ha; /* only used when suspending lun */
	port_id_t		d_id;
	uint16_t		loop_id;
	uint16_t		old_loop_id;
	int16_t			lun_cnt;
	uint16_t		dev_id;
#define FC_NO_LOOP_ID		0x100
	uint8_t			node_name[WWN_SIZE];	/* Big Endian. */
	uint8_t			port_name[WWN_SIZE];	/* Big Endian. */
	uint8_t			mp_byte;	/* multi-path byte */
    	uint8_t			cur_path;	/* current path id */
	int			port_login_retry_count;
	int			login_retry;
	atomic_t		state;		/* state for I/O routing */
#define FC_DEVICE_DEAD		1		/* Device has been missing for the expired time */
									/* "port timeout" */
#define FC_DEVICE_LOST		2		/* Device is missing */
#define FC_ONLINE		3		/* Device is ready and online */

	uint32_t		flags;
#define	FC_FABRIC_DEVICE	BIT_0
#define	FC_TAPE_DEVICE		BIT_1
#define	FC_INITIATOR_DEVICE	BIT_2
#define	FC_CONFIG		BIT_3

#define	FC_VSA			BIT_4
#define	FC_HD_DEVICE		BIT_5
#define	FC_SUPPORT_RPT_LUNS	BIT_6
#define FC_XP_DEVICE            BIT_7

#define FC_CONFIG_DEVICE        BIT_8
#define FC_MSA_DEVICE           BIT_9
#define FC_MSA_PORT_ACTIVE      BIT_10
#define FC_FAILBACK_DISABLE    	BIT_11

#define FC_LOGIN_NEEDED		BIT_12
#define FC_EVA_DEVICE           BIT_13
#define FC_FAILOVER_DISABLE    	BIT_14
#define FC_DSXXX_DEVICE         BIT_15
#define FC_AA_EVA_DEVICE        BIT_16
#define FC_AA_MSA_DEVICE        BIT_17
#define FC_DFXXX_DEVICE          BIT_18
#define FC_AUTH_REQ	    	BIT_19
 
	int16_t		 	cfg_id;		/* index into cfg device table */
	uint16_t	notify_type;
	atomic_t		port_down_timer;
	int	(*fo_combine)(void *, uint16_t, 
		struct fc_port *, uint16_t );
	int	(*fo_detect)(void);
	int	(*fo_notify)(void);
	int	(*fo_select)(void);
	int (*fo_target_port)(struct fc_port *, struct fc_lun *, int);

	fc_port_type_t	port_type;

	lun_bit_mask_t	lun_mask;
} fc_port_t;

/*
 * Fibre channel LUN structure.
 */
typedef struct fc_lun {
        struct list_head	list;

	fc_port_t		*fcport;
	uint16_t		lun;
	uint8_t			max_path_retries;
	uint8_t			flags;
#define	FC_DISCON_LUN		BIT_0
#define	FC_VISIBLE_LUN		BIT_2
#define	FC_ACTIVE_LUN		BIT_3
	uint8_t			inq0;
	uint8_t 		asymm_support;	
#define	 TGT_PORT_GRP_UNSUPPORTED  	0		
#define	 SET_TGT_PORT_GRP_UNSUPPORTED   1		
	u_long			kbytes;
	u_long      		s_time;	/* service time */ 
	u_long			io_cnt; /* total xfer count since boot */
	void			*mplun;	
	void			*mpbuf;	/* ptr to buffer use by 
					   multi-path driver */
	int			mplen;
    	uint8_t 		path_id; /* path id */
} fc_lun_t;


/*
 * Registered State Change Notification structures.
 */
typedef struct {
    port_id_t d_id;
    uint8_t format;
} rscn_t;

/*
 * Flash Database structures.
 */
#define FLASH_DATABASE_0        0x1c000
#define FLASH_DATABASE_1        0x18000
#define FLASH_DATABASE_VERSION  1

typedef struct
{
    uint32_t seq;
    uint8_t  version;
    uint8_t  checksum;
    uint16_t size;
    uint8_t  spares[8];
}flash_hdr_t;

typedef struct
{
    uint8_t name[WWN_SIZE];
    uint8_t  spares[8];
}flash_node_t;

typedef struct
{
    flash_hdr_t  hdr;
    flash_node_t node[MAX_FIBRE_DEVICES];
}flash_database_t;

/*
 * SNS structures.
 */
#define	RFT_CMD_SIZE	60
#define	RFT_DATA_SIZE	16

#define	RFF_CMD_SIZE	32
#define	RFF_DATA_SIZE	16

#define GAN_CMD		0x100
#define	GAN_CMD_SIZE	28
#define	GAN_DATA_SIZE	(620 + 16)

#define	GID_CMD		0x1a1
#define	GID_CMD_SIZE	28
#define	GID_DATA_SIZE	(MAX_FIBRE_DEVICES * 4 + 16)
#define GID_NX_PORT	0x7F

#define GPN_CMD		0x112
#define	GPN_CMD_SIZE	28
#define	GPN_DATA_SIZE	(8 + 16)

#define GNN_CMD		0x113
#define	GNN_CMD_SIZE	28
#define	GNN_DATA_SIZE	(8 + 16)

#define	RNN_CMD_SIZE	36
#define	RNN_DATA_SIZE	16

typedef struct {
	union {
		struct {
			uint16_t buffer_length;
			uint16_t reserved_1;
			uint32_t buffer_address[2];
			uint16_t subcommand_length;
			uint16_t reserved_2;
			uint16_t subcommand;
			uint16_t size;
			uint32_t reserved_3;
			uint8_t param[36];
		} cmd;

		uint8_t gan_rsp[GAN_DATA_SIZE];
		uint8_t gid_rsp[GID_DATA_SIZE];
		uint8_t gpn_rsp[GPN_DATA_SIZE];
		uint8_t gnn_rsp[GNN_DATA_SIZE];
		uint8_t rft_rsp[RFT_DATA_SIZE];
		uint8_t rff_rsp[RFF_DATA_SIZE];
		uint8_t rnn_rsp[RNN_DATA_SIZE];
	} p;
} sns_cmd_rsp_t;

struct dev_id {
	uint8_t	al_pa;
	uint8_t	area;
	uint8_t	domain;
#if defined(EXTENDED_IDS)
	uint8_t	reserved;
	uint16_t loop_id;
	uint16_t reserved_1;	/* ISP24XX         -- 8 bytes. */
#else
	uint8_t	loop_id;
#endif
}; 
/* GET_ID_LIST_SIZE */
#define MAX_ID_LIST_SIZE (sizeof(struct dev_id) * MAX_FIBRE_DEVICES)

/*
 * FC-CT interface
 *
 * NOTE: All structures are in big-endian in form.
 */

#define CT_REJECT_RESPONSE      0x8001
#define CT_ACCEPT_RESPONSE      0x8002

#define RFT_ID_CMD      0x217
#define RFT_ID_REQ_SIZE (16 + 4 + 32)
#define RFT_ID_RSP_SIZE 16

#define RFF_ID_CMD      0x21F
#define RFF_ID_REQ_SIZE (16 + 4 + 2 + 1 + 1)
#define RFF_ID_RSP_SIZE 16

#define RNN_ID_CMD      0x213
#define RNN_ID_REQ_SIZE (16 + 4 + 8)
#define RNN_ID_RSP_SIZE 16

#define RSNN_NN_CMD      0x239
#define RSNN_NN_REQ_SIZE (16 + 8 + 1 + 255)
#define RSNN_NN_RSP_SIZE 16


/* CT command header -- request/response common fields */
struct ct_cmd_hdr {
	uint8_t revision;
	uint8_t in_id[3];
	uint8_t gs_type;
	uint8_t gs_subtype;
	uint8_t options;
	uint8_t reserved;
};

/* CT command request */
struct ct_sns_req {
	struct ct_cmd_hdr header;
	uint16_t	command;
	uint16_t	max_rsp_size;
	uint32_t	reserved;

	union {
		/* GA_NXT, GPN_ID, GNN_ID, GFT_ID */
		struct {
			uint8_t reserved;
			uint8_t	port_id[3];
		} port_id;

		struct {
			uint8_t port_type;
			uint8_t	domain;
			uint8_t	area;
			uint8_t	reserved;
		} gid_pt;

		struct {
			uint8_t reserved;
			uint8_t	port_id[3];
			uint8_t	fc4_types[32];
		} rft_id;

		struct {
			uint8_t reserved;
			uint8_t	port_id[3];
			uint16_t reserved2;
			uint8_t	fc4_feature;
			uint8_t	fc4_type;
		} rff_id;

		struct {
			uint8_t reserved;
			uint8_t	port_id[3];
			uint8_t	node_name[8];
		} rnn_id;

		struct {
			uint8_t	node_name[8];
			uint8_t	name_len;
			uint8_t	sym_node_name[255];
		} rsnn_nn;
	} req;
};

/* CT command response header */
struct ct_rsp_hdr {
	struct ct_cmd_hdr header;
	uint16_t	response;
	uint16_t	residual;
	uint8_t		reserved;
	uint8_t		reason_code;
	uint8_t		explanation_code;
	uint8_t		vendor_unique;
};

struct ct_sns_gid_pt_data {
	uint8_t	control_byte;
	uint8_t	port_id[3];
};

struct ct_sns_rsp {
	struct ct_rsp_hdr header;

	union {
		struct {
			uint8_t	port_type;
			uint8_t	port_id[3];
			uint8_t	port_name[8];
			uint8_t	sym_port_name_len;
			uint8_t	sym_port_name[255];
			uint8_t	node_name[8];
			uint8_t	sym_node_name_len;
			uint8_t	sym_node_name[255];
			uint8_t	init_proc_assoc[8];
			uint8_t	node_ip_addr[16];
			uint8_t	class_of_service[4];
			uint8_t	fc4_types[32];
			uint8_t ip_address[16];
			uint8_t	fabric_port_name[8];
			uint8_t	reserved;
			uint8_t	hard_address[3];
		} ga_nxt;

		struct {
			struct ct_sns_gid_pt_data entries[MAX_FIBRE_DEVICES];
		} gid_pt;

		struct {
			uint8_t	port_name[8];
		} gpn_id;

		struct {
			uint8_t	node_name[8];
		} gnn_id;

		struct {
			uint8_t	fc4_types[32];
		} gft_id;
	} rsp;
};

struct ct_sns_pkt {
	union {
		struct ct_sns_req req;
		struct ct_sns_rsp rsp;
	} p;
};

/*
 * Structure used in Get Port List mailbox command (0x75).
 */
typedef struct
{
    uint8_t    name[WWN_SIZE];
    uint16_t   loop_id;
}port_list_entry_t;

/*
 * Structure used for device info.
 */
typedef struct
{
    uint8_t    name[WWN_SIZE];
    uint8_t    wwn[WWN_SIZE];
    uint16_t   loop_id;
    uint8_t    port_id[3];
}device_data_t;

/* Mailbox command completion status */
#define MBS_PORT_ID_IN_USE              0x4007
#define MBS_LOOP_ID_IN_USE              0x4008
#define MBS_ALL_LOOP_IDS_IN_USE         0x4009
#define MBS_NAME_SERVER_NOT_LOGGED_IN   0x400A


#define MAX_IOCTL_WAIT_THREADS	32
typedef struct _wait_q_t {
	uint8_t			flags;
#define WQ_IN_USE	0x1

	struct semaphore	wait_q_sem;
	struct _wait_q_t	*pnext;
} wait_q_t;

typedef struct hba_ioctl{

	/* Ioctl cmd serialization */
	uint16_t	access_bits; /* bits should be used atomically */
#define IOCTL_ACTIVE	1 /* first bit */
#define IOCTL_WANT	2 /* 2nd bit */

	spinlock_t	wait_q_lock; /* IOCTL wait_q Queue Lock */
	wait_q_t	wait_q_arr[MAX_IOCTL_WAIT_THREADS];
	wait_q_t	*wait_q_head;
	wait_q_t	*wait_q_tail;

	/* Passthru cmd/completion */
	struct semaphore	cmpl_sem;
	struct timer_list	cmpl_timer;
	uint8_t		ioctl_tov;
	uint8_t		SCSIPT_InProgress;
	uint8_t		MSIOCB_InProgress;

	os_tgt_t	*ioctl_tq;
	os_lun_t	*ioctl_lq;

	/* AEN queue */
	void		*aen_tracking_queue;/* points to async events buffer */
	uint8_t		aen_q_head;	/* index to the current head of q */
	uint8_t		aen_q_tail;	/* index to the current tail of q */

	/* Misc. */
	uint32_t	flags;
#define	IOCTL_OPEN			BIT_0
#define	IOCTL_AEN_TRACKING_ENABLE	BIT_1
	uint8_t		*scrap_mem;	/* per ha scrap buf for ioctl usage */
	uint32_t	scrap_mem_size; /* total size */
	uint32_t	scrap_mem_used; /* portion used */

} hba_ioctl_context;

/* Mailbox command semaphore queue for command serialization */
typedef struct _mbx_cmdq_t {
	struct semaphore	cmd_sem;
	struct _mbx_cmdq_t	*pnext;
} mbx_cmdq_t;

/*
 * Linux Host Adapter structure
 */
typedef struct scsi_qla_host
{
	/* Linux adapter configuration data */
	struct Scsi_Host *host;             /* pointer to host data */
	struct scsi_qla_host   *next;

	device_reg_t	*iobase;		/* Base I/O address */
	unsigned long	pio_address;
	unsigned long	pio_length;
	void *		mmio_address;
	unsigned long	mmio_length;
#define MIN_IOBASE_LEN		0x100

	struct pci_dev   *pdev;
	uint8_t          devnum;
	u_long            host_no;
	u_long            instance;
	uint8_t           revision;
	uint8_t           ports;
	u_long            actthreads;
	u_long            ipreq_cnt;
	u_long            qthreads;
	u_long            spurious_int;
	uint32_t        total_isr_cnt;		/* Interrupt count */
	uint32_t        total_isp_aborts;	/* controller err cnt */
	uint32_t        total_lip_cnt;		/* LIP cnt */
	uint32_t	total_dev_errs;		/* device error cnt */
	uint32_t	total_ios;		/* IO cnt */
	uint64_t	total_bytes;		/* xfr byte cnt */

	uint64_t	total_input_cnt;	/* input request cnt */
	uint64_t	total_output_cnt;	/* output request cnt */
	uint64_t	total_ctrl_cnt;		/* control request cnt */
	uint64_t	total_input_bytes;	/* input xfr bytes cnt */
	uint64_t	total_output_bytes;	/* output xfr bytes cnt */

	uint32_t	total_mbx_timeout;	/* mailbox timeout cnt */
	uint32_t 	total_loop_resync; 	/* loop resyn cnt */

	/* Adapter I/O statistics for failover */
	uint64_t	IosRequested;
	uint64_t	BytesRequested;
	uint64_t	IosExecuted;
	uint64_t	BytesExecuted;

	uint32_t         device_id;
	uint16_t         subsystem_vendor;
	uint16_t         subsystem_device;
 
	/* ISP connection configuration data */
	uint16_t         max_public_loop_ids;
	uint16_t         min_external_loopid; /* First external loop Id */
	uint8_t          current_topology; /* Current ISP configuration */
	uint8_t          prev_topology;    /* Previous ISP configuration */
                     #define ISP_CFG_NL     1
                     #define ISP_CFG_N      2
                     #define ISP_CFG_FL     4
                     #define ISP_CFG_F      8
	uint8_t         id;                 /* Host adapter SCSI id */
    	uint8_t		qfull_retry_delay;
	uint16_t        loop_id;       /* Host adapter loop id */
	port_id_t       d_id;           /* Host adapter port id */

	uint8_t         operating_mode;  /* current F/W operating mode */
	                                 /* 0 - LOOP, 1 - P2P, 2 - LOOP_P2P,
	                                  * 3 - P2P_LOOP
	                                  */
	uint8_t         active_fc4_types;/* active fc4 types */
	uint8_t         current_speed;   /* current F/W operating speed */
    	uint8_t		qfull_retry_count;

	/* NVRAM configuration data */
	uint16_t        loop_reset_delay;   /* Loop reset delay. */
	uint16_t        hiwat;              /* High water mark per device. */
	uint16_t        execution_throttle; /* queue depth */ 
	uint16_t        minimum_timeout;    /* Minimum timeout. */
	uint8_t         retry_count;
	uint8_t         login_timeout;
	int             port_down_retry_count;
	uint8_t         loop_down_timeout;
	uint16_t        max_probe_luns;
	uint16_t        max_luns;
	uint16_t        max_targets;
	uint16_t        nvram_base;
	uint16_t        nvram_size;
	
	/* Fibre Channel Device List. */
        struct list_head	fcports;

	/* OS target queue pointers. */
	os_tgt_t		*otgt[MAX_FIBRE_DEVICES];

	uint32_t          flash_db;         /* Flash database address in use. */
	uint32_t          flash_seq;        /* Flash database seq # in use. */
	volatile uint16_t lip_seq;          /* LIP sequence number. */
	
	  /* RSCN queue. */
	rscn_t rscn_queue[MAX_RSCN_COUNT];
	uint8_t rscn_in_ptr;
	uint8_t rscn_out_ptr;
	
	unsigned long  last_irq_cpu; /* cpu where we got our last irq */

	/*
	 * Need to hold the list_lock with irq's disabled in order to
	 * access the following list.
	 * This list_lock is of lower priority than the io_request_lock.
	 */
	/*********************************************************/
        spinlock_t              list_lock  ____cacheline_aligned;      
                                           /* lock to guard lists which 
						   hold srb_t's*/
        struct list_head        retry_queue;    /* watchdog queue */
        struct list_head        done_queue;     /* job on done queue */
        struct list_head        failover_queue; /* failover list link. */
	struct list_head        free_queue;     /* SRB free queue */
	struct list_head        scsi_retry_queue;     /* SCSI retry queue */
	
	struct list_head        pending_queue;	/* SCSI command pending queue */
	
        /*********************************************************/


	/* Linux kernel thread */
	struct task_struct  *dpc_handler;     /* kernel thread */
	struct semaphore    *dpc_wait;       /* DPC waits on this semaphore */
	struct semaphore    *dpc_notify;     /* requester waits for DPC on this semaphore */
	struct semaphore    dpc_sem;       /* DPC's semaphore */
	uint8_t dpc_active;                  /* DPC routine is active */

	/* Received ISP mailbox data. */
	volatile uint16_t mailbox_out[MAILBOX_REGISTER_COUNT];

	/* This spinlock is used to protect "io transactions", you must	
	 * aquire it before doing any IO to the card, eg with RD_REG*() and
	 * WRT_REG*() for the duration of your entire commandtransaction.
	 *
	 * This spinlock is of lower priority than the io request lock.
	 */

	spinlock_t		hardware_lock ____cacheline_aligned;

	/* Outstandings ISP commands. */
	srb_t           *outstanding_cmds[MAX_OUTSTANDING_COMMANDS];
	uint32_t current_outstanding_cmd; 

	/* ISP ring lock, rings, and indexes */
	dma_addr_t	request_dma;        /* Physical address. */
	request_t       *request_ring;      /* Base virtual address */
	request_t       *request_ring_ptr;  /* Current address. */
	uint16_t        req_ring_index;     /* Current index. */
	uint16_t        req_q_cnt;          /* Number of available entries. */

	dma_addr_t	response_dma;       /* Physical address. */
	response_t      *response_ring;     /* Base virtual address */
	response_t      *response_ring_ptr; /* Current address. */
	uint16_t        rsp_ring_index;     /* Current index. */
    
#if defined(ISP2300)
	/* Data for IP support */
	uint8_t		ip_port_name[WWN_SIZE];

	struct risc_rec_entry *risc_rec_q;	/* RISC receive queue */
	dma_addr_t	risc_rec_q_dma;		/*  physical address */
	uint16_t	rec_entries_in;
	uint16_t	rec_entries_out;

	struct send_cb	*active_scb_q[MAX_SEND_PACKETS];
	uint32_t	current_scb_q_idx;

	uint32_t	mtu;
	uint16_t	header_size;
	uint16_t        max_receive_buffers;
	struct buffer_cb *receive_buffers;
	uint32_t	receive_buff_data_size;

	void		(*send_completion_routine)
				(struct send_cb *scb);
	void		*receive_packets_context;
	void		(*receive_packets_routine)
				(void *context, struct buffer_cb *bcb);
	void		*notify_context;
	void		(*notify_routine)
				(void *context, uint32_t type);
#endif

	/* Firmware Initialization Control Block data */
	dma_addr_t	init_cb_dma;         /* Physical address. */
	struct init_cb       *init_cb;
  
	/* Timeout timers. */
	uint8_t         queue_restart_timer;   
	atomic_t        loop_down_timer;         /* loop down timer */
	uint8_t         loop_down_abort_time;    /* port down timer */
	uint8_t		link_down_timeout;       /* link down timeout */
	uint32_t        timer_active;
	uint32_t        forceLip;
	struct timer_list        timer;

	/* These are used by mailbox operations. */
	mbx_cmd_t	*mcp;
	unsigned long	mbx_cmd_flags;
#define MBX_CMD_ACTIVE	1 /* first bit */
#define MBX_CMD_WANT	2 /* 2nd bit */
#define MBX_INTERRUPT	3 /* 3rd bit */
#define MBX_INTR_WAIT   4 /* 4rd bit */
#define MBX_UPDATE_FLASH_ACTIVE 5 /* 5th bit */

	spinlock_t	mbx_reg_lock;   /* Mbx Cmd Register Lock */
	spinlock_t	mbx_q_lock;     /* Mbx Active Cmd Queue Lock */
	spinlock_t	mbx_bits_lock;  /* Mailbox access bits Lock */

	uint32_t	mbx_lock_bits;  /* controlled by mbx_bits_lock */
#define MBX_CMD_LOCK	1 /* first bit */
#define MBX_CMD_WANT	2 /* 2nd bit */

	struct semaphore  mbx_intr_sem;  /* Used for completion notification */

	mbx_cmdq_t	*mbx_sem_pool_head;  /* Head Pointer to a list of
			                      * recyclable mbx semaphore pool
			                      * to be used during run time.
			                      */
	mbx_cmdq_t	*mbx_sem_pool_tail;  /* Tail Pointer to semaphore pool*/
#define MBQ_INIT_LEN	16 /* initial mbx sem pool q len. actual len may vary */

	mbx_cmdq_t	*mbx_q_head; /* Head Pointer to sem q for active cmds */
	mbx_cmdq_t	*mbx_q_tail; /* Tail Pointer to sem q for active cmds */


        uint32_t	retry_q_cnt; 
        uint32_t	scsi_retry_q_cnt; 
        uint32_t	failover_cnt; 

	uint8_t	*cmdline;

        uint32_t	login_retry_count; 
        
    
	volatile struct
	{
		uint32_t     online                  :1;   /* 0 */
		uint32_t     enable_64bit_addressing :1;   /* 1 */
		uint32_t     mbox_int                :1;   /* 2 */
		uint32_t     mbox_busy               :1;   /* 3 */

		uint32_t     port_name_used          :1;   /* 4 */
		uint32_t     failover_enabled        :1;   /* 5 */
		uint32_t     failback_disabled	     :1;   /* 6 */
		uint32_t     cfg_suspended   	     :1;   /* 7 */

		uint32_t     disable_host_adapter    :1;   /* 8 */
		uint32_t     rscn_queue_overflow     :1;   /* 9 */
		uint32_t     reset_active            :1;   /* 10 */
		uint32_t     link_down_error_enable  :1;   /* 11 */

		uint32_t     disable_risc_code_load  :1;   /* 12 */
		uint32_t     set_cache_line_size_1   :1;   /* 13 */
		uint32_t     enable_target_mode      :1;   /* 14 */
		uint32_t     disable_luns            :1;   /* 15 */

		uint32_t     enable_lip_reset        :1;   /* 16 */
		uint32_t     enable_lip_full_login   :1;   /* 17 */
		uint32_t     enable_target_reset     :1;   /* 18 */
		uint32_t     updated_fc_db           :1;   /* 19 */

		uint32_t     enable_flash_db_update  :1;   /* 20 */
		uint32_t     in_isr                  :1;   /* 21 */
		uint32_t     dpc_sched               :1;   /* 23 */

		uint32_t     nvram_config_done       :1;   /* 24 */
		uint32_t     update_config_needed    :1;   /* 25 */
		uint32_t     management_server_logged_in    :1; /* 26 */
#if defined(ISP2300)
                uint32_t     enable_ip               :1;   /* 27 */
#endif
		uint32_t     process_response_queue  :1;   /* 28 */
		uint32_t     enable_led_scheme	     :1;   /* 29 */	
	} flags;

	uint32_t     device_flags;
#define DFLG_LOCAL_DEVICES		BIT_0
#define DFLG_RETRY_LOCAL_DEVICES	BIT_1
#define DFLG_FABRIC_DEVICES		BIT_2
#define	SWITCH_FOUND			BIT_3
#define	DFLG_NO_CABLE			BIT_4

	unsigned long	cpu_flags;

        uint8_t		marker_needed; 
	uint8_t		missing_targets;
	uint8_t		sns_retry_cnt;
	uint8_t		cmd_wait_cnt;
	uint8_t		mem_err;

	unsigned long   dpc_flags;
#define	RESET_MARKER_NEEDED	0	/* initiate sending a marker to ISP */
#define	RESET_ACTIVE		1
#define	ISP_ABORT_NEEDED	2	/* initiate ISP Abort */
#define	ABORT_ISP_ACTIVE	3	/* isp abort in progress */

#define	LOOP_RESYNC_NEEDED	4	/* initiate a configure fabric sequence */
#define	LOOP_RESYNC_ACTIVE	5
#define	COMMAND_WAIT_NEEDED	6
#define	COMMAND_WAIT_ACTIVE	7

#define LOCAL_LOOP_UPDATE       8	/* Perform a local loop update */
#define RSCN_UPDATE             9	/* Perform a RSCN update */
#define MAILBOX_RETRY           10
#define ISP_RESET_NEEDED        11	/* Initiate a ISP reset ??? */

#define FAILOVER_EVENT_NEEDED   12
#define FAILOVER_EVENT		13
#define FAILOVER_NEEDED   	14
#define LOOP_RESET_NEEDED	15

#define DEVICE_RESET_NEEDED	16
#define DEVICE_ABORT_NEEDED	17
#define SCSI_RESTART_NEEDED	18	/* Processes any requests in scsi retry queue */
#define PORT_RESTART_NEEDED	19	/* Processes any requests in retry queue */

#define RESTART_QUEUES_NEEDED	20	/* Restarts requeusts in the lun queue */
#define ABORT_QUEUES_NEEDED	21
#define RELOGIN_NEEDED	        22
#define LOGIN_RETRY_NEEDED	23	/* initiates any fabric logins that are required */ 

#define REGISTER_FC4_NEEDED	24	/* set when need to register again.*/
#define TASKLET_SCHED		25	/* Tasklet is scheduled.  */ 
#define DONE_RUNNING		26	/* Done task is running. */
#define ISP_ABORT_RETRY         27      /* ISP aborted. */

#define PORT_SCAN_NEEDED      28      /* */
#define IOCTL_ERROR_RECOVERY	29      
#define LOOP_DOWN_IO_RECOVERY   30

/* macro for timer to start dpc for handling mailbox commands */
#define MAILBOX_CMD_NEEDED	(LOOP_RESET_NEEDED|DEVICE_RESET_NEEDED|   \
    DEVICE_ABORT_NEEDED|ISP_ABORT_NEEDED)

	/* These 3 fields are used by the reset done in dpc thread */
	uint16_t	reset_bus_id;
	uint16_t	reset_tgt_id;
	uint16_t	reset_lun;

	uint8_t		interrupts_on;
	uint8_t		init_done;

	atomic_t		loop_state;
#define LOOP_TIMEOUT 1
#define LOOP_DOWN    2
#define LOOP_UP      3
#define LOOP_UPDATE  4
#define LOOP_READY   5
#define LOOP_DEAD    6  /* Link Down Timer expires */

	mbx_cmd_t 	mc;
	uint32_t	mbx_flags;
#define  MBX_IN_PROGRESS  BIT_0
#define  MBX_BUSY       BIT_1 /* Got the Access */
#define  MBX_SLEEPING_ON_SEM  BIT_2 
#define  MBX_POLLING_FOR_COMP  BIT_3
#define  MBX_COMPLETED      BIT_4
#define  MBX_TIMEDOUT       BIT_5 
#define  MBX_ACCESS_TIMEDOUT BIT_6

/* following are new and needed for IOCTL support */
	hba_ioctl_context *ioctl;
	uint8_t     node_name[WWN_SIZE];
	uint8_t     port_name[WWN_SIZE];

	/* PCI expansion ROM image information. */
	unsigned long	code_types;
#define ROM_CODE_TYPE_BIOS	0
#define ROM_CODE_TYPE_FCODE	1
#define ROM_CODE_TYPE_EFI	3

	uint8_t		bios_revision[2];
	uint8_t		efi_revision[2];
	uint8_t		fcode_revision[16];
	uint32_t        fw_revision[4];

	uint8_t     nvram_version; 

	void        *ioctl_mem;
	dma_addr_t  ioctl_mem_phys;
	uint32_t    ioctl_mem_size;
	uint32_t    isp_abort_cnt;

	/* HBA serial number */
	uint8_t     serial0;
	uint8_t     serial1;
	uint8_t     serial2;

	/* NVRAM Offset 200-215 : Model Number */
	uint8_t    model_number[16];

	/* oem related items */
	uint8_t oem_fru[8];
	uint8_t oem_ec[8];

	uint32_t    dump_done;
	unsigned long    done_q_cnt;
	unsigned long    pending_in_q;

	uint32_t failover_type;
	uint32_t failback_delay;
	unsigned long   cfg_flags;
#define	CFG_ACTIVE	0	/* CFG during a failover, event update, or ioctl */
#define	CFG_FAILOVER	1	/* CFG during path change */
	/* uint8_t	cfg_active; */
	int	eh_start;

	uint32_t 	 iocb_hiwat;
	uint32_t 	 iocb_cnt;
	uint32_t 	 iocb_overflow_cnt;
	
	int	srb_cnt;
	int	srb_alloc_cnt;	/*Number of allocated SRBs  */

	uint32_t mbox_trace;

	uint32_t	binding_type;
#define BIND_BY_PORT_NAME	0
#define BIND_BY_PORT_ID		1

	srb_t	*status_srb;    /* Keep track of Status Continuation Entries */

	uint32_t	dropped_frame_error_cnt;

	/* Basic Firmware related info */
	uint8_t		fw_version[3]; /* 0 : major 1: minor 2: subminor */
	uint16_t	fw_attributes;
	uint32_t	fw_memory_size;
	uint32_t	fw_transfer_size;

	uint16_t	fw_options1;
	uint16_t	fw_options2;
	uint16_t	fw_options3;
	struct qla2x00_seriallink_firmware_options fw_seriallink_options;
	uint16_t	fw_seriallink_options24[4];

#if !defined(ISP2100) && !defined(ISP2200)
	/* Needed for BEACON */
	uint8_t		beacon_blink_led;
	uint8_t		beacon_color_state;
#define QLA_LED_GRN_ON		0x01
#define QLA_LED_YLW_ON		0x02
#define QLA_LED_ABR_ON		0x04
#define QLA_LED_BCN_ON		0x06	/* isp24xx/25xx beacon: yellow,amber */
#define QLA_LED_RGA_ON		0x07    /* isp2322: red, green, amber */
#endif

	ms_iocb_entry_t         *ms_iocb;
	dma_addr_t              ms_iocb_dma;
	void			*ct_iu;
	dma_addr_t		ct_iu_dma;

	Scsi_Cmnd	*ioctl_err_cmd;	 

	unsigned long 	fdmi_flags;
#define FDMI_REGISTER_NEEDED	0 /* bit 0 */

	/* Hardware ID/version string from NVRAM */
	uint8_t		hw_id_version[16];
	/* Model description string from our table based on NVRAM spec */
	uint8_t		model_desc[80];

	uint16_t	product_id[4];
	int             fw_dumped;
	void            *fw_dump24;
	uint32_t        fw_dump24_len;

	uint32_t cmd_wait_delay;

 	/* Scsi midlayer lock */
#if defined(SH_HAS_HOST_LOCK)
 	spinlock_t		host_lock ____cacheline_aligned;
#endif
} scsi_qla_host_t;

#if defined(__BIG_ENDIAN)
/* Big endian machine correction defines. */
#define	LITTLE_ENDIAN_16(x)	qla2x00_chg_endian((uint8_t *)&(x), 2)
#define	LITTLE_ENDIAN_24(x)	qla2x00_chg_endian((uint8_t *)&(x), 3)
#define	LITTLE_ENDIAN_32(x)	qla2x00_chg_endian((uint8_t *)&(x), 4)
#define	LITTLE_ENDIAN_64(x)	qla2x00_chg_endian((uint8_t *)&(x), 8)
#define	BIG_ENDIAN_16(x)
#define	BIG_ENDIAN_24(x)
#define	BIG_ENDIAN_32(x)
#define	BIG_ENDIAN_64(x)

#else
/* Little endian machine correction defines. */
#define	LITTLE_ENDIAN_16(x)
#define	LITTLE_ENDIAN_24(x)
#define	LITTLE_ENDIAN_32(x)
#define	LITTLE_ENDIAN_64(x)
#define	BIG_ENDIAN_16(x)	qla2x00_chg_endian((uint8_t *)&(x), 2)
#define	BIG_ENDIAN_24(x)	qla2x00_chg_endian((uint8_t *)&(x), 3)
#define	BIG_ENDIAN_32(x)	qla2x00_chg_endian((uint8_t *)&(x), 4)
#define	BIG_ENDIAN_64(x)	qla2x00_chg_endian((uint8_t *)&(x), 8)

#endif

/*
 * Macros to help code, maintain, etc.
 */
#define	LOOP_TRANSITION(ha)	( test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) || \
				  test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) )

#define	LOOP_NOT_READY(ha)	 ( (test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) || \
				    test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags) || \
                                    test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) || \
				    test_bit(LOOP_RESYNC_ACTIVE, &ha->dpc_flags) || \
				    test_bit(COMMAND_WAIT_NEEDED, &ha->dpc_flags) || \
                                    test_bit(COMMAND_WAIT_ACTIVE, &ha->dpc_flags)) ||  \
				 ha->loop_state == LOOP_DOWN)
				 
#define	LOOP_RDY(ha)	 ( !LOOP_NOT_READY(ha) )

#define	TGT_Q(ha, t)		(ha->otgt[t])
#define	LUN_Q(ha, t, l)		(TGT_Q(ha, t)->olun[l])
#define GET_LU_Q(ha, t, l)  ( (TGT_Q(ha,t) != NULL)? TGT_Q(ha, t)->olun[l] : NULL)
#define PORT_LOGIN_RETRY(fcport)    ((fcport)->port_login_retry_count)

#define MBOX_TRACE(ha,b)		{(ha)->mbox_trace |= (b);}

#define	MBS_MASK			0x3fff
#define	MBS_END				0x100
#define	QLA2X00_SUCCESS		(MBS_COMMAND_COMPLETE & MBS_MASK)
#define	QLA2X00_FAILED		(MBS_END + 2)
#define	QLA2X00_FUNCTION_FAILED		(MBS_END + 2)

#define  KMEM_ZALLOC(siz,id)	kmem_zalloc((siz), GFP_ATOMIC, (id) )
#define  KMEM_FREE(ip,siz)	kfree((ip))

#if defined(__cplusplus)
}
#endif

void qla2x00_device_queue_depth(scsi_qla_host_t *, Scsi_Device *);
#endif

#define BEACON_BLINK_NEEDED	30

/*
 *  Linux - SCSI Driver Interface Function Prototypes.
 */
int qla2x00_ioctl(Scsi_Device *, int , void *);
int qla2x00_proc_info ( char *, char **, off_t, int, int, int);
const char * qla2x00_info(struct Scsi_Host *host);
int qla2x00_detect(Scsi_Host_Template *);
int qla2x00_release(struct Scsi_Host *);
const char * qla2x00_info(struct Scsi_Host *);
int qla2x00_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
int qla2x00_abort(Scsi_Cmnd *);
int qla2x00_reset(Scsi_Cmnd *, unsigned int);
int qla2x00_biosparam(Disk *, kdev_t, int[]);
void qla2x00_intr_handler(int, void *, struct pt_regs *);
void qla24xx_intr_handler(int, void *, struct pt_regs *);
#if !defined(MODULE)
static int __init qla2100_setup (char *s);
#else
void qla2x00_setup(char *s);
#endif


/*
 * Scsi_Host_template (see hosts.h) 
 * Device driver Interfaces to mid-level SCSI driver.
 */

/* Kernel version specific template additions */

/* Number of segments 1 - 65535 */
#define SG_SEGMENTS     32             /* Cmd entry + 6 continuations */

/*
 * Scsi_Host_template (see hosts.h) 
 * Device driver Interfaces to mid-level SCSI driver.
 */

/* Kernel version specific template additions */

/*
 * max_sectors
 *
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,8)
#define TEMPLATE_MAX_SECTORS	max_sectors: 512,
#else
#define TEMPLATE_MAX_SECTORS 
#endif
/*
 * use_new_eh_code
 *
 */
#define TEMPLATE_USE_NEW_EH_CODE use_new_eh_code: 1,
/*
 * emulated
 *
 */
#define TEMPLATE_EMULATED emulated: 0,
/*
 * next
 *
 */
#define TEMPLATE_NEXT next: NULL,
/*
 * module
 *
 */
#define TEMPLATE_MODULE module: NULL,
/*
 * proc_dir
 *
 */
#define TEMPLATE_PROC_DIR proc_dir: NULL,

/* highmem_io */
#ifdef SHT_HAS_HIGHMEM_IO
#define TEMPLATE_HIGHMEM_IO	highmem_io: 1,
#else
#define TEMPLATE_HIGHMEM_IO
#endif
/* can_dma_32 */
#ifdef SHT_HAS_CAN_DMA_32
#define TEMPLATE_CAN_DMA_32	can_dma_32: 1,
#else
#define TEMPLATE_CAN_DMA_32
#endif
/* single_sg_ok A.S. 2.1 */
#ifdef SHT_HAS_SINGLE_SG_OK
#define TEMPLATE_SINGLE_SG_OK	single_sg_ok: 1,
#else
#define TEMPLATE_SINGLE_SG_OK
#endif
/* can_do_varyio -- A.S. 2.1 */
#ifdef SHT_HAS_CAN_DO_VARYIO
#define TEMPLATE_CAN_DO_VARYIO	can_do_varyio: 1,
#else
#define TEMPLATE_CAN_DO_VARYIO
#endif
/* vary_io -- SLES 8 */
#ifdef SHT_HAS_VARY_IO
#define TEMPLATE_VARY_IO	vary_io: 1,
#else
#define TEMPLATE_VARY_IO
#endif

/* RHEL3 specifics */

/* need_plug_timer -- RHEL3 */
#ifdef SHT_HAS_NEED_PLUG_TIMER
/* As per RH, backout scsi-affine feature. */
#define TEMPLATE_NEED_PLUG_TIMER
/*#define TEMPLATE_NEED_PLUG_TIMER need_plug_timer: 1, */
#else
#define TEMPLATE_NEED_PLUG_TIMER
#endif

/* diskdump */
#if defined(CONFIG_DISKDUMP) || defined(CONFIG_DISKDUMP_MODULE)
#define TEMPLATE_DISK_DUMP	disk_dump: 1,
#else
#define TEMPLATE_DISK_DUMP
#endif

/*
 * There are several Scsi_Host members that are RHEL3 specific
 * yet depend on the SCSI_HAS_HOST_LOCK define for visibility.
 * Unfortuantely, it seems several RH kernels have the define
 * set, but do not have a host_lock member.
 *
 * Use the SH_HAS_HOST_LOCK define determined during driver
 * compilation rather than SCSI_HAS_HOST_LOCK.
 *
 * Also use SH_HAS_CAN_QUEUE_MASK to determine if can_queue_mask
 * is an Scsi_Host member.
 *
 *	SH_HAS_HOST_LOCK	-- host_lock defined
 *	SH_HAS_CAN_QUEUE_MASK	-- can_queue_mask defined
 */

/* As per RH, backout scsi-affine feature. */
#undef SH_HAS_CAN_QUEUE_MASK

#define QLA2100_LINUX_TEMPLATE {				\
TEMPLATE_NEXT 	 	 	 	 	 	 	\
TEMPLATE_MODULE 	  	 	 	 	 	\
TEMPLATE_PROC_DIR 	  	 	 	 	 	\
	proc_info: qla2x00_proc_info,	                        \
	name:		"QLogic Fibre Channel 2x00",		\
	detect:		qla2x00_detect,				\
	release:	qla2x00_release,			\
	info:		qla2x00_info,				\
	ioctl: qla2x00_ioctl,                                    \
	command: NULL,						\
	queuecommand: qla2x00_queuecommand,			\
	eh_strategy_handler: NULL,				\
	eh_abort_handler: qla2xxx_eh_abort,			\
	eh_device_reset_handler: qla2xxx_eh_device_reset,	\
	eh_bus_reset_handler: qla2xxx_eh_bus_reset,		\
	eh_host_reset_handler: qla2xxx_eh_host_reset,		\
	abort: NULL,						\
	reset: NULL,						\
	slave_attach: NULL,					\
	bios_param: qla2x00_biosparam,				\
	can_queue: REQUEST_ENTRY_CNT+128, /* max simultaneous cmds      */\
	this_id: -1,		/* scsi id of host adapter    */\
	sg_tablesize: SG_SEGMENTS,	/* max scatter-gather cmds */\
	cmd_per_lun: 3,		/* cmds per lun (linked cmds) */\
	present: 0,		/* number of 7xxx's present   */\
	unchecked_isa_dma: 0,	/* no memory DMA restrictions */\
TEMPLATE_USE_NEW_EH_CODE 	 	 	 	 	\
TEMPLATE_MAX_SECTORS						\
TEMPLATE_EMULATED						\
TEMPLATE_HIGHMEM_IO						\
TEMPLATE_CAN_DMA_32						\
TEMPLATE_SINGLE_SG_OK						\
TEMPLATE_CAN_DO_VARYIO						\
TEMPLATE_VARY_IO						\
TEMPLATE_NEED_PLUG_TIMER					\
	use_clustering: ENABLE_CLUSTERING,			\
TEMPLATE_DISK_DUMP						\
}

#if defined(CONFIG_DISKDUMP) || defined(CONFIG_DISKDUMP_MODULE)
#undef	add_timer
#define	add_timer	diskdump_add_timer
#undef	del_timer_sync
#define	del_timer_sync	diskdump_del_timer
#undef	del_timer
#define	del_timer	diskdump_del_timer
#undef	mod_timer
#define	mod_timer	diskdump_mod_timer
#undef	schedule_timeout
#define schedule_timeout diskdump_schedule_timeout

#define spin_unlock_irq_dump(host_lock)			\
	do {						\
		if (crashdump_mode())			\
			spin_unlock(host_lock);		\
		else					\
			spin_unlock_irq(host_lock);	\
	} while (0)
#else
#define spin_unlock_irq_dump(host_lock)	spin_unlock_irq(host_lock)
#endif

#endif /* _IO_HBA_QLA2100_H */

