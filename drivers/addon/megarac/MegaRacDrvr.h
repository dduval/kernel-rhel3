/*****************************************************************************
 *
 *  MegaRacDrvr.h : MegaRac device driver definitions
 *
 ****************************************************************************/
#ifndef MEGARAC_DRIVER_H
#define MEGARAC_DRIVER_H

/****************************************************************
 * firmware include files - pack all structures in these files
 ***************************************************************/
#pragma pack(1)                             /* supported by all current compilers */

#include "agpintfc.h"
#include "ccbhdlr.h"                 
#include "dmi.h"
#include "errcod.h"                 
#include "mg90xx.h"
#include "misc.h"
#include "rcsadmin.h"                 
#include "rcsic.h"                 
#include "redirect.h"                 

#pragma pack()

/*****************************************************************************
 * make determination about what is being compiled 
 ****************************************************************************/
#include "MegaRacWho.h"

/*****************************************************************************
 *
 *  For Driver development, each MegaRacDrvr*.[ch] must
 *  define the following macros (the following are Netware examples):
 *      #define MEMSET( dst,val,cnt)                        CSetB( val, dst, cnt )
 *      #define MEMZERO(dst,cnt)                            CSetB( 0,   dst, cnt )
 *      #define MEMCPY( dst,src,cnt)                        CMovB( src, dst, cnt )
 *      #define MALLOC_CONTIGUOUS(raci,ptr,cnt)             ...too complicated...
 *      #define   FREE_CONTIGUOUS(raci,ptr)                 NPA_Return_Memory( raci pOsInfo->npaHandle, ptr )
 *      #define VIRTUAL_TO_PHYSICAL(raci,physAddr,virtAddr) physAddr = (void*)MapDataOffsetToAbsoluteAddress(virtAddr)
 *      #define MICRO_SECOND_DELAY(cnt)                     NPA_Micro_Delay( cnt )
 *      #define  READ_RAC_UCHAR(raci,addr)                   In8( raci pOsInfo->busTag, raci addr )
 *      #define  READ_RAC_ULONG(raci,addr)                  In32( raci pOsInfo->busTag, raci addr )
 *      #define WRITE_RAC_UCHAR(raci,addr,val)              Out8( raci pOsInfo->busTag, raci addr, val )
 *      #define WRITE_RAC_ULONG(raci,addr,val)             Out32( raci pOsInfo->busTag, raci addr, val )
 *
 ****************************************************************************/

/*******************************************
 * driver debug masks, see MegaRacDebug.h
 ******************************************/
#define MEGA_ENTRY          0x0001
#define MEGA_ISR            0x0002
#define MEGA_DPCISR         0x0004
#define MEGA_STARTIO        0x0008
#define MEGA_TIMEOUT        0x0010
#define MEGA_DEVICE         0x0020
#define MEGA_EXIT           0x0040
#define MEGA_EVENT          0x0080
#define MEGA_INSIDE_ISR     0x0100
#define MEGA_CURSOR         0x0200
#define MEGA_POLL_MODE  0x00020000

/*******************************************
 * common driver structures
 ******************************************/

typedef enum {                              /* for use with racEvent() */
    racEventOpen                    = -3,   /*****open and close not currently used*********/
    racEventClose,
    racEventBoardDead,                      /* 07.12.99, Jose says: not used */
    racEventFirmwareRequest,                /* raw RAC_INFO.firmwareRequest* to process  */
    /* the following must match #define's in firmware/redirect.h */
    racEventGetDCInfo               = 1,    
    racEventGetDCPacket_X,                  /* =2, Jose says: not used */
    racEventSendDCComplete,
    racEventSendKey,                        /* =4, Jose says: not used */
    racEventSendMouse,
    racEventSendAlertToHost,                /* =6, Jose says: not used */
    racEventRequestHostOsShutdown,
    racEventModeChangedToGraphics,          /* =8, Jose says: not used */
    racEventServiceDYRM,
    racEventPassThruData,
    racEventCursorChange,                   /* internal to driver/api */
    racEventMaximum = racEventCursorChange  /* remember to update when adding events!!!! */
} RAC_EVENT;

typedef enum {                              /* see racPreProcessCCB() */
    racDrvrErrNone,
    racDrvrErrPending,
    racDrvrErrInvalidParameter,
    racDrvrErrNotImplemented
} RAC_DRVR_ERR;

typedef struct {                            /* see MegaRacSetEvents() and MegaRacWaitEvents() */
    unsigned long eventHandle[racEventMaximum+1];
} RAC_EVENT_NOTIFICATION;
#define RAC_EVENT_NOTIFICATION_NO_CHANGE    ( 0)
#define RAC_EVENT_NOTIFICATION_DELETE       (~0)

typedef enum {
        RAC_ATTACHMENTS_CMD_CUR,            /* get current value without any changes */
        RAC_ATTACHMENTS_CMD_INC,
        RAC_ATTACHMENTS_CMD_DEC
} RAC_ATTACHMENTS_CMD;

typedef struct {                            /* see MegaRacAttachments() */
    RAC_ATTACHMENTS_CMD         cmd;
    unsigned long               previous;
    unsigned long               currentt;   /* if correctly spelled ("current") then */
} RAC_ATTACHMENTS;                          /* ...linux smp driver complains */

typedef struct _RAC_INFO {
    struct _RAC_OS_INFO        *pOsInfo;
    unsigned char              *portAddrBase;
    unsigned char              *portAddrHIMR;
    unsigned char              *portAddrHIFR;
    unsigned char              *portAddrHCMDR;
    unsigned long              *portAddrDATA;
    unsigned char              *portAddrHASR;
    unsigned char              *portAddrHFR;
    unsigned char              *portAddrHCR;
    CCB_Header                 *currentCCB;
    BOOL                        resetInProgress;
    BOOL                        specialModeInProgress;
    BOOL                        shutDownIssued;
    BOOL                        heartbeatStarted;
    BOOL                        okToProcessRCS;
    unsigned int                heartbeatCount;
    unsigned int                boardAliveCount;
    unsigned long               firmwareRequestData;     /* (hfr & HIFR_FIRM_REQ)==1 */
    unsigned short              firmwareRequestDCPacket; /* from GetDCPacket_x_FRI */
    DMI_HEADER                  dmiHeader;
    PCI_COMMON_CONFIG           pciCommonConfig;
    RAC_EVENT_NOTIFICATION      events;
} RAC_INFO;

/*******************************************
 * MegaRacDrvr.c function definitions
 ******************************************/
#ifdef MEGARAC_DRIVER_IS
#if DEBUG_PRINT
    static char        *racIoctlToString        ( unsigned long   ioControlCode );
    static char        *racFriToString          ( int             fri );
    static void         racDumpRegs             ( RAC_INFO *pRAC, char *str );
#endif
static void             racSetAddrs             ( RAC_INFO *pRAC, void *baseAddr );
static void             racClearAddrs           ( RAC_INFO *pRAC );
static void             racFindDmiInBios        ( RAC_INFO *pRAC );
static BOOL             racHandleReportDMI      ( RAC_INFO *pRAC, CCB_Header *pCCB );
static BOOL             racSendCcb              ( RAC_INFO *pRAC, CCB_Header *pCCB, unsigned long ioControlCode );
static BOOL             racSendCcbWait          ( RAC_INFO *pRAC, CCB_Header *pCCB, unsigned long ioControlCode );
static void             racSendIoBaseAddr       ( RAC_INFO *pRAC );
static void             racSendStartHeartBeat   ( RAC_INFO *pRAC );
static void             racSendStopHeartBeat    ( RAC_INFO *pRAC );
static void             racSendHardReset        ( RAC_INFO *pRAC );
static void             racStartupFinal         ( RAC_INFO *pRAC );
static void             racShutdownBegin        ( RAC_INFO *pRAC );
static void             racPreProcessCCB        ( RAC_INFO *pRAC, CCB_Header *pCCB, unsigned long  bufLen,
                                                                                    unsigned long  ioControlCode,
                                                                                    RAC_DRVR_ERR  *drvrStatus );
static BOOL             racIsrDpc               ( RAC_INFO *pRAC );
static void             racTimerTick            ( RAC_INFO *pRAC );
static void             racEvent                ( RAC_INFO *pRAC, RAC_EVENT   rawEvent );

extern void           (*racOsEventProc)         ( RAC_INFO *pRAC, RAC_EVENT   rawEvent, RAC_EVENT firmEvent );
extern void           (*racOsSetEventsProc)     ( RAC_INFO *pRAC, CCB_Header *pCCB );
extern void           (*racOsAttachmentsProc)   ( RAC_INFO *pRAC, CCB_Header *pCCB );
extern unsigned short (*racGetHardwareCursor)   ( void );
#endif  /* ifdef MEGARAC_DRIVER_IS */

/*******************************************
 * MegaRac Device Registers and Definitions 
 ******************************************/

/* RAC Register Offsets */
#define HOST_INTERRUPT_MASK_REG     0x01    /* alias HIMR_   */
#define HOST_INTERRUPT_FLAG_REG     0x02    /* alias HIFR_   */
#define HOST_COMMAND_REG            0x03    /* alias HCMDR_  */
#define HOST_DATA_IN_REG            0x04
#define HOST_DATA_OUT_REG           0x04
#define HOST_ADAPTER_STATUS_REG     0x09    /* alias HASR_   */
#define HOST_FLAGS_REG              0x0a    /* same as HIFR_ */
#define HOST_CONTROL_REG            0x10    /* alias HCR_    */

/* HIMR - Host Interrupt Mask Register */
#define HIMR_DISABLE_ALL            0x00    /* disable all interrupts */
#define HIMR_TO_HOST                0x80    /* enable interrupt to host */
#define HIMR_FIRMWARE_REQ           0x08    /* enable firmware request int */
#define HIMR_HOST_COMP              0x04    /* Host Adapter Cmd Execute Cmplt */
#define HIMR_DEFAULTS             ( HIMR_TO_HOST | HIMR_FIRMWARE_REQ | HIMR_HOST_COMP )
#define HIMR_DEFAULTS_NO_HOST     ( HIMR_DEFAULTS & ~HIMR_TO_HOST )

/* HIFR - Host Interrupt Flag Register and
   HFR  - Host Flag Register           are the same except for HIFR_ANY_INTR */
#define HIFR_ANY_INTR               0x80    /* 'Any Interrupt' occurred */
#define HIFR_SOFT_INT               0x40    /* 'Software Generated Interrupt by local processor' */
#define HIFR_DATA_OUT               0x20    /* host data out port empty */
#define HIFR_FIRM_REQ               0x08    /* firmware request interrupt */
#define HIFR_HACC                   0x04    /* Host Adapter Cmd Execute Cmplt */
#define HIFR_FIRM_HANDSHAKE         0x02    /* firmware request handshake */

/* HCMDR - Host Command Register */
#define HCMDR_ISSUE_RCS_CMD         ISSUE_RCS
#define HCMDR_ENTER_SDK_FLASH_MODE  SDK_NEWFLASH    
#define HCMDR_SET_HACC              SET_COMMAND_COMPLETE

/* HASR - Host Adapter Status Register */
#define HASR_FIRM_READY             0x80    /* firmware ready for command */
#define HASR_FIRM_ABORT             0x40    /* firmware aborted */
#define HASR_FIRM_FAULT             0x20    /* firmware fault */
#define HASR_FIRM_INVALID           0x10    /* firmware invalid */
#define HASR_FIRM_FAILED          ( HASR_FIRM_ABORT | HASR_FIRM_FAULT | HASR_FIRM_INVALID )

/* HCR - Host Control Register */
#define HCR_HARD_RESET              0x80    /* Hard Reset */
#define HCR_SOFT_RESET              0x40    /* Soft Reset */
#define HCR_INTR_RESET              0x20    /* reset all interrupts in HIFR */
#define HCR_SOFT_INTR_1             0x10    /* host soft interrupt 1 */

/* misc */
#define BOARD_ALIVE_RESET              0
#define AMI_VENDOR_ID             0x101e    /* see struct _PCI_COMMON_CONFIG.VendorID */
#define AMI_MEGA_RAC_ID           0x9063    /* see struct _PCI_COMMON_CONFIG.DeviceID */


#endif /* MEGARAC_DRIVER_H */
