/*******************************************************************

    File Name   :   AGPINTFC.H

    Author      :   K.V.Subash & Parthiban Baskar

    Date        :   12 December 1997

    Purpose     :   Equates and data types used by the AGP Interface

    Copyright   :   American Megatrends Inc., 1997-1998

    TAB Spacing :   4

********************************************************************/

#ifndef  __AGPINTFC_H__
#define  __AGPINTFC_H__

#include "dtypes.h"
#include "rtc.h"
#include "rcs.h"

/*  Command values written to the local command register    */
# define AGP_COMMAND_GROUP  0x10

#define AGP_NOP                     AGP_COMMAND_GROUP
#define AGP_SET_TEXTVIDEO_ADDRESS   AGP_COMMAND_GROUP + 1
#define AGP_SET_TEXTVIDEO_FORMAT    AGP_COMMAND_GROUP + 2
#define AGP_START_TEXTVIDEO_SNOOP   AGP_COMMAND_GROUP + 3
#define AGP_STOP_TEXTVIDEO_SNOOP    AGP_COMMAND_GROUP + 4
#define AGP_START_SMI               AGP_COMMAND_GROUP + 5
#define AGP_STOP_SMI                AGP_COMMAND_GROUP + 6
#define GET_REDIRECTION_STATUS      AGP_COMMAND_GROUP + 7

#define SMI_GENERATION_PORT     ((unsigned char *)0xFD400000)

#define SMI_HANDSHAKE           0x01
#define ENABLE_SMI              0x04
#define DISABLE_SMI             0x00

/*  The following defines for Host Video System are duplicated in
    startup\src\fixddata.c for operational ease.  If they change here,
    they have to change there too     */
/*  Defines for Host Video system   */
#define VGA     0
#define AGP     1

/*  Defines for the new method of videosnooping for AGP systems, with the
    ability to treat NT/Novell crash screens as special cases   */
#define STOP_SMI_RECD       2
#define CANT_SMI_NOW        3
#define SMI_TIMEOUT         4
#define GRAPHICS_WITHOUT_DRIVER     5
#define EXIT_AGPSCREENDUMP  0x0F

/*  Copied from mg90xx.h    */
# define LOCAL_INTR_FLAG_REG    ((unsigned char *)0xC0000800)
# define LOCAL_INTR_GEN_REG     ((unsigned char *)0xC0000802)
# define LOCAL_COMMAND_REG      ((unsigned char *)0xC0000803)
# define LOCAL_DATA_REG         ((unsigned long *)0xC0000804)
# define LOCAL_INTR_MASK_REG    ((unsigned char *)0xC0000809)
# define LOCAL_FLAG_REG         ((unsigned char *)0xC000080C)

# define SOFTINT2       0x80
# define SRST           0x40
# define SOFTINT1       0x10
# define HCPF           0x08
# define HDOF           0x04
# define ENABLE_INTR    0x01
# define INTR_SET       0x01
# define HACC           0x04

#define BLINK_ON        '5'
#define BLINK_OFF_HI    '2'
#define BLINK_OFF_LO    '5'
#define HI_VIDEO        '1'
#define LO_VIDEO_HI     '2'
#define LO_VIDEO_LO     '1'

typedef struct _TextScreenLogEntryHeaderTAG
{
    DATE_TIME   DateTimeStamp;
    u8        Rows, Columns;
    u8        CursorPosRow;
    u8        CursorPosCol;
    u8        Reserved [4];
} TEXT_SCREEN_LOG_ENTRY_HEADER;

typedef struct _TextScreenLogEntryTAG
{
    TEXT_SCREEN_LOG_ENTRY_HEADER   TextScreenHeader;
    u8    TextScreenBuffer [16000];
} TEXT_SCREEN_LOG_ENTRY;

typedef struct _TextScreenPktToRemoteTAG
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    TEXT_SCREEN_LOG_ENTRY_HEADER   TextScreenHeader;
    u8    NumberOfBytesPerDirtyBit;
    u8    NumberOfDirtyBytes;
    u8    ScreenData [16100];
} TEXT_SCREEN_PKT_TO_REMOTE;

typedef struct _TextScreenLogPktToRemoteTAG
{
    RCS_COMMAND_PACKET RCSCmdPkt;
    TEXT_SCREEN_LOG_ENTRY TextScreenPkt;

} TEXT_SCREEN_LOG_PKT_TO_REMOTE;

typedef struct _ReportCursorPosPktTAG
{
    u8        CursorPosRow;       // gives the current row
    u8        CursorPosCol;       // gives the current column
    TWOBYTES    LinearCursorPos;    // gives the current linear cursor pos
    //08/09/99  // LinearCursorPos should be 0xffff when the other two fields
    //Parts     //  are used.  If this is not 0xffff, then the top two fields
                //  will be ignored regardless of what they contain,
                //  and this value will be used.
} REPORT_CURSOR_POS_PKT;


/*
    Function prototypes
*/

extern  int     AGPInitializeModule (void);
extern  int     ReInitializeModules (void);
extern  int     AGPHandler (void * param);
extern  int     AGPScreenDump (void);
extern  int     AltGotoxy (unsigned int CurrentPosition);
extern  int     SetTextAttr (u8 Color);
extern  int     AGPProcessDone(void);
extern  int     ConnectionNotActive (u8 SessionIndex);
extern  int     SendTextScreenToRemote (TEXT_SCREEN_PKT_TO_REMOTE *
                        TextScreenPktToRemote, TWOBYTES Count);
extern  u8    GetCursorPosRow (void);
extern  u8    GetCursorPosCol (void);
extern  void    ReportCursorPos (RCS_COMMAND_PACKET * RCSCmdPkt);

#endif

