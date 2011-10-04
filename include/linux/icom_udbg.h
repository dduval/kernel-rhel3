/*
 * iCom_udbg.h
 *
 * Copyright (C) 2001 Michael Anderson, IBM Corporation
 *
 * Serial device driver include file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#define TRACE_BLK_SZ         1024
#define TRACE_WITH_DATA      0x80000000
#define TRACE_TIME           0x40000000
#define TRACE_GET_MEM        0x20000000
#define TRACE_RET_MEM        0x10000000
#define TRACE_GET_PORT_MEM   0x00000001
#define TRACE_FOD_ADDR       0x00000005
#define TRACE_FOD_XBUFF      0x00000006
#define TRACE_FID_ADDR       0x00000007
#define TRACE_FID_RBUFF      0x00000008
#define TRACE_RET_PORT_MEM   0x00000100
#define TRACE_LOAD_MEM       0x00000200
#define TRACE_CHANGE_SPEED   0x00000300
#define TRACE_PARENB         0x00000301
#define TRACE_PARODD         0x00000302
#define TRACE_XR_ENAB        0x00000303
#define TRACE_STARTUP        0x00000400
#define TRACE_CABLE_ID       0x00000401
#define TRACE_SHUTDOWN       0x00000500
#define TRACE_DEVICE_NUMB    0x00000600
#define TRACE_STARTUP_ERROR  0x00000601
#define TRACE_CLOSE          0x00000700
#define TRACE_CLOSE_HANGUP   0x00000701
#define TRACE_OPEN_ACTIVE    0x00000702
#define TRACE_WRITE          0x00000800
#define TRACE_WRITE_FULL     0x00000801
#define TRACE_WRITE_NODATA   0x00000802
#define TRACE_WRITE_START    0x00000803
#define TRACE_PUT_CHAR       0x00000900
#define TRACE_PUT_FULL       0x00000901
#define TRACE_FLUSH_CHAR     0x00000a00
#define TRACE_START_FLUSH    0x00000a01
#define TRACE_WRITE_ROOM     0x00000b00
#define TRACE_CHARS_IN_BUFF  0x00000c00
#define TRACE_CHARS_REMAIN   0x00000c01
#define TRACE_GET_MODEM      0x00000d00
#define TRACE_SET_MODEM      0x00000e00
#define TRACE_RAISE_RTS      0x00000e01
#define TRACE_RAISE_DTR      0x00000e02
#define TRACE_LOWER_RTS      0x00000e03
#define TRACE_LOWER_DTR      0x00000e04
#define TRACE_GET_SERIAL     0x00000f00
#define TRACE_SET_SERIAL     0x00001000
#define TRACE_SET_LSR        0x00001100
#define TRACE_IOCTL          0x00001200
#define TRACE_IOCTL_IGNORE   0x00001201
#define TRACE_SEND_XCHAR     0x00001300
#define TRACE_QUICK_WRITE    0x00001301
#define TRACE_THROTTLE       0x00001400
#define TRACE_UNTHROTTLE     0x00001500
#define TRACE_SET_TERMIOS    0x00001600
#define TRACE_STOP           0x00001700
#define TRACE_START          0x00001800
#define TRACE_HANGUP         0x00001900
#define TRACE_BREAK          0x00001a00
#define TRACE_WAIT_UNTIL_SENT  0x00001b00
#define TRACE_FLUSH_BUFFER     0x00001c00
#define TRACE_CHECK_MODEM      0x00001d00
#define TRACE_CTS_UP           0x00001d01
#define TRACE_CTS_DOWN         0x00001d02
#define TRACE_INTERRUPT        0x00001e00
#define TRACE_XMIT_COMPLETE    0x00001e01
#define TRACE_RCV_COMPLETE     0x00001e02
#define TRACE_FID_STATUS       0x00001e03
#define TRACE_RCV_COUNT        0x00001e04
#define TRACE_REAL_COUNT       0x00001e05
#define TRACE_BREAK_DET        0x00001e06
#define TRACE_IGNORE_CHAR      0x00001e07
#define TRACE_PARITY_ERROR     0x00001e08
#define TRACE_XMIT_DISABLED    0x00001e09
#define TRACE_WAKEUP           0x00001f00
#define TRACE_CLEAR_INTERRUPTS 0x0000ff00
#define TRACE_START_PROC_A     0x0000ff01
#define TRACE_START_PROC_B     0x0000ff02
#define TRACE_STOP_PROC_A      0x0000ff03
#define TRACE_STOP_PROC_B      0x0000ff04
#define TRACE_RAISE_DTR_RTS    0x0000ff05
#define TRACE_START_PROC_C     0x0000ff06
#define TRACE_START_PROC_D     0x0000ff07
#define TRACE_STOP_PROC_C      0x0000ff08
#define TRACE_STOP_PROC_D      0x0000ff09
#define TRACE_ENABLE_INTERRUPTS_PA 0x0000ff0a
#define TRACE_ENABLE_INTERRUPTS_PB 0x0000ff0b
#define TRACE_ENABLE_INTERRUPTS_PC 0x0000ff0c
#define TRACE_ENABLE_INTERRUPTS_PD 0x0000ff0d
#define TRACE_DIS_INTERRUPTS_PA 0x0000ff0e
#define TRACE_DIS_INTERRUPTS_PB 0x0000ff0f
#define TRACE_DIS_INTERRUPTS_PC 0x0000ff10
#define TRACE_DIS_INTERRUPTS_PD 0x0000ff11
#define TRACE_DROP_DTR_RTS   0x0000ff12

#define BAUD_TABLE_LIMIT             20
static int icom_acfg_baud[] = {
    300,
    600,
    900,
    1200,
    1800,
    2400,
    3600,
    4800,
    7200,
    9600,
    14400,
    19200,
    28800,
    38400,
    57600,
    76800,
    115200,
    153600,
    230400,
    307200,
    460800
};

struct icom_regs {
    u32                  control;        /* Adapter Control Register     */
    u32                  interrupt;      /* Adapter Interrupt Register   */
    u32                  int_mask;       /* Adapter Interrupt Mask Reg   */
    u32                  int_pri;        /* Adapter Interrupt Priority r */
    u32                  int_reg_b;      /* Adapter non-masked Interrupt */
    u32                  resvd01;
    u32                  resvd02;
    u32                  resvd03;
    u32                  control_2;      /* Adapter Control Register 2   */
    u32                  interrupt_2;    /* Adapter Interrupt Register 2 */
    u32                  int_mask_2;     /* Adapter Interrupt Mask 2     */
    u32                  int_pri_2;      /* Adapter Interrupt Prior 2    */
    u32                  int_reg_2b;     /* Adapter non-masked 2         */
};

struct func_dram {
    u32                 reserved[108];          /* 0-1B0   reserved by personality code */
    u32                 RcvStatusAddr;          /* 1B0-1B3 Status Address for Next rcv */
    u8                  RcvStnAddr;             /* 1B4     Receive Station Addr */
    u8                  IdleState;              /* 1B5     Idle State */
    u8                  IdleMonitor;            /* 1B6     Idle Monitor */
    u8                  FlagFillIdleTimer;      /* 1B7     Flag Fill Idle Timer */
    u32                 XmitStatusAddr;         /* 1B8-1BB Transmit Status Address */
    u8                  StartXmitCmd;           /* 1BC     Start Xmit Command */
    u8                  HDLCConfigReg;          /* 1BD     Reserved */
    u8                  CauseCode;              /* 1BE     Cause code for fatal error */
    u8                  xchar;                  /* 1BF     High priority send */
    u32                 reserved3;              /* 1C0-1C3 Reserved */
    u8                  PrevCmdReg;             /* 1C4     Reserved */
    u8                  CmdReg;                 /* 1C5     Command Register */
    u8                  async_config2;          /* 1C6     Async Config Byte 2*/
    u8                  async_config3;          /* 1C7     Async Config Byte 3*/
    u8                  dce_resvd[20];          /* 1C8-1DB DCE Rsvd           */
    u8                  dce_resvd21;            /* 1DC     DCE Rsvd (21st byte*/
    u8                  misc_flags;             /* 1DD     misc flags         */
#define V2_HARDWARE     0x40
#define ICOM_HDW_ACTIVE 0x01
    u8                  call_length;            /* 1DE     Phone #/CFI buff ln*/
    u8                  call_length2;           /* 1DF     Upper byte (unused)*/
    u32                 call_addr;              /* 1E0-1E3 Phn #/CFI buff addr*/
    u16                 timer_value;            /* 1E4-1E5 general timer value*/
    u8                  timer_command;          /* 1E6     general timer cmd  */
    u8                  dce_command;            /* 1E7     dce command reg    */
    u8                  dce_cmd_status;         /* 1E8     dce command stat   */
    u8                  x21_r1_ioff;            /* 1E9     dce ready counter  */
    u8                  x21_r0_ioff;            /* 1EA     dce not ready ctr  */
    u8                  x21_ralt_ioff;          /* 1EB     dce CNR counter    */
    u8                  x21_r1_ion;             /* 1EC     dce ready I on ctr */
    u8                  rsvd_ier;               /* 1ED     Rsvd for IER (if ne*/
    u8                  ier;                    /* 1EE     Interrupt Enable   */
    u8                  isr;                    /* 1EF     Input Signal Reg   */
    u8                  osr;                    /* 1F0     Output Signal Reg  */
    u8                  reset;                  /* 1F1     Reset/Reload Reg   */
    u8                  disable;                /* 1F2     Disable Reg        */
    u8                  sync;                   /* 1F3     Sync Reg           */
    u8                  error_stat;             /* 1F4     Error Status       */
    u8                  cable_id;               /* 1F5     Cable ID           */
    u8                  cs_length;              /* 1F6     CS Load Length     */
    u8                  mac_length;             /* 1F7     Mac Load Length    */
    u32                 cs_load_addr;           /* 1F8-1FB Call Load PCI Addr */
    u32                 mac_load_addr;          /* 1FC-1FF Mac Load PCI Addr  */
};

/*
 * adapter defines and structures
 */
#define ICOM_CONTROL_START_A         0x00000008
#define ICOM_CONTROL_STOP_A          0x00000004
#define ICOM_CONTROL_START_B         0x00000002
#define ICOM_CONTROL_STOP_B          0x00000001
#define ICOM_CONTROL_START_C         0x00000008
#define ICOM_CONTROL_STOP_C          0x00000004
#define ICOM_CONTROL_START_D         0x00000002
#define ICOM_CONTROL_STOP_D          0x00000001
#define ICOM_IRAM_OFFSET             0x1000
#define ICOM_DCE_IRAM_OFFSET         0x0A00
#define ICOM_CABLE_ID_VALID          0x01
#define ICOM_CABLE_ID_MASK           0xF0
#define ICOM_DISABLE                 0x80
#define CMD_XMIT_RCV_ENABLE          0xC0
#define CMD_XMIT_ENABLE              0x40
#define CMD_RCV_DISABLE              0x00
#define CMD_RCV_ENABLE               0x80
#define CMD_RESTART                  0x01
#define CMD_HOLD_XMIT                0x02
#define CMD_SND_BREAK                0x04
#define RS232_CABLE                  0x06
#define V24_CABLE                    0x0E
#define V35_CABLE                    0x0C
#define V36_CABLE                    0x02
#define NO_CABLE                     0x00
#define START_DOWNLOAD               0x80
#define ICOM_INT_MASK_PRC_A          0x00003FFF
#define ICOM_INT_MASK_PRC_B          0x3FFF0000
#define ICOM_INT_MASK_PRC_C          0x00003FFF
#define ICOM_INT_MASK_PRC_D          0x3FFF0000
#define INT_RCV_COMPLETED            0x1000
#define INT_XMIT_COMPLETED           0x2000
#define INT_IDLE_DETECT              0x0800
#define INT_RCV_DISABLED             0x0400
#define INT_XMIT_DISABLED            0x0200
#define INT_RCV_XMIT_SHUTDOWN        0x0100
#define INT_FATAL_ERROR              0x0080
#define INT_CABLE_PULL               0x0020
#define INT_SIGNAL_CHANGE            0x0010
#define HDLC_PPP_PURE_ASYNC          0x02
#define HDLC_FF_FILL                 0x00
#define HDLC_HDW_FLOW                0x01
#define START_XMIT                   0x80
#define ICOM_ACFG_DRIVE1             0x20
#define ICOM_ACFG_NO_PARITY          0x00
#define ICOM_ACFG_PARITY_ENAB        0x02
#define ICOM_ACFG_PARITY_ODD         0x01
#define ICOM_ACFG_8BPC               0x00
#define ICOM_ACFG_7BPC               0x04
#define ICOM_ACFG_6BPC               0x08
#define ICOM_ACFG_5BPC               0x0C
#define ICOM_ACFG_1STOP_BIT          0x00
#define ICOM_ACFG_2STOP_BIT          0x10
#define ICOM_DTR                     0x80
#define ICOM_RTS                     0x40
#define ICOM_RI                      0x08
#define ICOM_DSR                     0x80
#define ICOM_DCD                     0x20
#define ICOM_CTS                     0x40

#define NUM_XBUFFS 1
#define NUM_RBUFFS 2
#define RCV_BUFF_SZ 0x0200
#define XMIT_BUFF_SZ 0x1000
struct statusArea {
    /**********************************************/
    /* Transmit Status Area                       */
    /**********************************************/
    struct {
        u32                    leNext;         /* Next entry in Little Endian on Adapter */
        u32                    leNextASD;
        u32                    leBuffer;       /* Buffer for entry in LE for Adapter */
        u16                    leLengthASD;
        u16                    leOffsetASD;
        u16                    leLength;       /* Length of data in segment */
        u16                    flags;
#define SA_FLAGS_DONE           0x0080          /* Done with Segment */
#define SA_FLAGS_CONTINUED      0x8000          /* More Segments */
#define SA_FLAGS_IDLE           0x4000          /* Mark IDLE after frm */
#define SA_FLAGS_READY_TO_XMIT  0x0800
#define SA_FLAGS_STAT_MASK      0x007F
    } xmit[NUM_XBUFFS];

    /**********************************************/
    /* Receive Status Area                        */
    /**********************************************/
    struct {
        u32                    leNext;         /* Next entry in Little Endian on Adapter */
        u32                    leNextASD;
        u32                    leBuffer;       /* Buffer for entry in LE for Adapter */
        u16                    WorkingLength;  /* size of segment */
        u16                    reserv01;
        u16                    leLength;       /* Length of data in segment */
        u16                    flags;
#define SA_FL_RCV_DONE           0x0010          /* Data ready */
#define SA_FLAGS_OVERRUN         0x0040
#define SA_FLAGS_PARITY_ERROR    0x0080 
#define SA_FLAGS_FRAME_ERROR     0x0001
#define SA_FLAGS_FRAME_TRUNC     0x0002
#define SA_FLAGS_BREAK_DET       0x0004    /* set conditionally by device driver, not hardware */
#define SA_FLAGS_RCV_MASK        0xFFE6
    } rcv[NUM_RBUFFS];
};

struct icom_port {
    u8                    imbed_modem;
#define   ICOM_UNKNOWN       1
#define   ICOM_RVX           2
#define   ICOM_IMBED_MODEM   3
    unsigned char         cable_id;
    int                   open_active_count;
    struct tty_struct     *tty;
    unsigned long         event;
    struct tq_struct      tqueue;
    int                   flags;
    int                   xmit_fifo_size;
    int                   baud_base;
    wait_queue_head_t     close_wait;
    wait_queue_head_t     open_wait;
    wait_queue_head_t     delta_msr_wait;
    int                   blocked_open;
    unsigned short        close_delay;
    unsigned short        closing_wait;
    unsigned long         timeout;
    long                  session; /* Session of opening process */
    long                  pgrp; /* pgrp of opening process */
    unsigned char         read_status_mask;
    unsigned char         ignore_status_mask;
    struct async_icount	  icount;	
    struct termios        normal_termios;
    struct termios        callout_termios;
    unsigned long         int_reg;
    struct icom_regs      *global_reg;
    struct func_dram      *dram;
    int                   adapter;
    int                   port;
    int                   minor_number;
    struct statusArea     *statStg;
    dma_addr_t            statStg_pci;
    u32                   *xmitRestart;
    dma_addr_t            xmitRestart_pci;
    unsigned char         *xmit_buf;
    dma_addr_t            xmit_buf_pci;
    unsigned char         *recv_buf;
    dma_addr_t            recv_buf_pci;
    int                   next_rcv;
    int                   put_length;
    int                   passed_diags;
    int                   status;
#define ICOM_PORT_ACTIVE  1 /* Port exists. */
#define ICOM_PORT_OFF     0 /* Port does not exist. */
    int                   load_in_progress;
    u32                   tpr;
#define NUM_ERROR_ENTRIES 16
    u32                   error_data[NUM_ERROR_ENTRIES];
    u32                   thread_status;
#define STATUS_INIT 0x99999999
#define STATUS_PASS 0 
    unsigned long         *trace_blk;
};

struct icom_adapter {
    unsigned long      base_addr;
    unsigned long      base_addr_pci;
    unsigned char      slot;
    unsigned char      irq_number;
    struct pci_dev     *pci_dev;
    struct icom_port   port_info[4];
    int                version;
#define ADAPTER_V1   0x0001
#define ADAPTER_V2   0x0002
    u32                subsystem_id;
#define FOUR_PORT_MODEL				0x02521014
#define V2_TWO_PORTS_RVX			0x021A1014
#define V2_ONE_PORT_RVX_ONE_PORT_IMBED_MDM	0x02511014
    int                numb_ports;
    unsigned int       saved_bar;
    unsigned int       saved_command_reg;
    u32                tpr;
    u32                error_data[NUM_ERROR_ENTRIES];
    u32                diag_int1;
    u32                diag_int2;
    u32                diag_int_pri1;
    u32                diag_int_pri2;
    u32                diag_int_reset1;
    u32                diag_int_reset2;
    u32                resources;
#define HAVE_MALLOC_1	        0x00000001
#define HAVE_INT_HANDLE		0x00000010
    u32                *malloc_addr_1;
    dma_addr_t         malloc_addr_1_pci;
};

/* prototype */
void iCom_sercons_init(void);

