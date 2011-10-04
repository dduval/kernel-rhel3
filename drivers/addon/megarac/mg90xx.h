#ifndef __MG90XX_H__
#define __MG90XX_H__

/* Timeout value in seconds for RAC card to become ready after a cold start */
#define RAC_CARD_READY_TIMEOUT	120

# define LOCAL_INTR_FLAG_REG    ((unsigned char *)0xC0000800)
# define HOST_INTR_GEN_REG     ((unsigned char *)0xC0000802)
# define LOCAL_COMMAND_REG		((unsigned char *)0xC0000803)
# define LOCAL_DATA_REG		((unsigned long *)0xC0000804)
# define LOCAL_CONTROL_REG   ((unsigned char *)0xC0000808)
# define LOCAL_INTR_MASK_REG	((unsigned char *)0xC0000809)
# define LOCAL_FLAG_REG		((unsigned char *)0xC000080C)
 
# define MG9063_NEW_FUNCTION_REG ((unsigned long*)0xC0001800)
# define LOCAL_INTR_MASK_REG_2   ((unsigned char*)0xC0001804)
#define  LOCAL_FLAG_REG_2   ((unsigned char*)0xC0001806)
#define  CLASS_CODE_REG     ((unsigned char*)0xC0001808)
#define  SUBSYSTEM_REG     ((unsigned char*)0xC000180C)


# define MG9063_HOST_PCI_PAGE_WINDOW_1 ((unsigned short*)0xC0001810)
# define MG9063_HOST_PCI_PAGE_WINDOW_2 ((unsigned short*)0xC0001812)
# define MG9063_HOST_PCI_PAGE_WINDOW_3 ((unsigned short*)0xC0001814)
# define MG9063_HOST_PCI_PAGE_WINDOW_4 ((unsigned short*)0xC0001816)

# define CRT_REG2_DATA_REG              ((unsigned char*)0xC0001818)
# define CRT_REG7_DATA_REG              ((unsigned char*)0xC0001819)
# define CRT_REG9_DATA_REG              ((unsigned char*)0xC000181A)
# define CRT_REGC_DATA_REG              ((unsigned char*)0xC000181B)
# define CRT_REGD_DATA_REG              ((unsigned char*)0xC000181C)
# define CRT_REG12H_DATA_REG            ((unsigned char*)0xC000181D)
# define PORT80_DATA_REG                ((unsigned char*)0xC000181E)
# define PCI_CACHE_LINE_REG             ((unsigned char*)0xC000181F)


# define SOFTINT2	0x80
# define SRST		0x40
#define  TIMEOUT     0x20
# define SOFTINT1	0x10
# define HCPF		0x08
# define HDOF    	0x04
# define ENABLE_INTR	0x01
# define INTR_SET       0x01
# define HACC		0x04

# define VGAEIN  0x01
# define MONOEIN 0x02
# define POSTEIN 0x04
# define RSTEIN  0x08
# define _3DEEIN 0x10
# define _3D5EIN 0x40



# define SET_SOFTINT    0x40
# define SET_FRI        0x08
# define SET_HACC       0x04
# define FRI_PENDING    0x02    // FRI Handshaking
# define SET_SMI        0x01    // SMI Handshaking

#define NFR_IO_CYCLE                     0x00000001
#define NFR_DISABLE_PCI_RETRY            0x00000004
#define NFR_NO_RESET_ON_PCI_RESET        0x00000010
#define NFR_IOCHRDY_ENABLED              0x00000040
#define NFR_GENERAL_TMOUT_ENABLED        0x00000080
#define NFR_PCI_DAC_CYCLE_ENABLED        0x00100000
#define NFR_ACCESS_REG_THROUGH_PCI       0x00200000
#define NFR_ROWSIZ12_DISABLE             0x00800000
#define NFR_DRAM_TYPE_9060               0X00000000
#define NFR_DRAM_TYPE_64MB               0x01000000
#define NFR_DRAM_TYPE_16MB               0x02000000
#define NFR_DRAM_TYPE_4MB                0x03000000
#define NFR_PCI_READ_AHEAD_DISABLED      0x04000000
#define NFR_PCI_VGA_3D4                  0x20000000
#define NFR_REF16MHZ                     0x40000000
#define NFR_FLUSHFIFO                    0x80000000



#define IPND_MG9063_LOCAL_INT            0x0
#define IPND_MG9063_TIMER_INT            0x1
#define IPND_I2C_INT                     0x2
#define IPND_RTC_INT                     0x3
#define IPND_NET_INT                     0x4
#define IPND_PCMCIA_INT                  0x5
#define IPND_SERIAL_INT                  0x6
#define IPND_RS485_INT                   0x7
#define IPND_CLOCK_INT                   0x8
#define IPND_TMR1_INT                    0x9

#define SERIES_767 767
#define SERIES_780 780
#define SERIES_788 788




/*  Command values written to the local command register    */
#define ISSUE_CCB 0x01
#define ISSUE_RCS 0x02
# define AGP_SCREEN_DUMP    0x0A
#define	 SET_COMMAND_COMPLETE   0x77 //for the host driver to tell us to set HACC while its going 									//out
#define HEARTBEAT_ENABLE 0x78
#define HEARTBEAT_DISABLE 0x79
#define DEBUG_MODE_ON 0x88
#define DEBUG_MODE_OFF 0x89
#define ABORT_RECOVERY_ON   0x90
#define ABORT_RECOVERY_OFF  0x91
#define VSP_COMMAND_GROUP  0x80

#define SDK_NEWFLASH   		0xA0




#define HOST_TIMEDOUT  0x10
#define HOST_NORMAL    0x0

#endif

