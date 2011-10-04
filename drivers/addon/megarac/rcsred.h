/*******************************************************************

    File Name   :   RCSRED.H

    Author      :   K.V.Subash & Parthiban Baskar

    Date        :   8 December 1997

    Purpose     :   RCS Redirection Cmd grp codes

	Copyright	:	American Megatrends Inc., 1997-1998

********************************************************************/

#ifndef __RCSRED_H__
#define __RCSRED_H__
#include "rtc.h"
#include "rcs.h"
/************************************************************************/
/**************  REMOTE CONSOLE REDIRECTION COMMANDS ********************/
/************************************************************************/


/*******************************GROUP 020**************************/
#define RCS_RED_START_CONSOLE_REDIRECTION       0X0200
#define RCS_RED_STOP_CONSOLE_REDIRECTION        0X0201
#define RCS_RED_SEND_VIDEO_MODE                 0x0202
#define RCS_RED_SEND_TEXT_SCREEN                0x0203
#define RCS_RED_SEND_KEY                        0x0204
#define RCS_RED_READ_KEY                        0x0205  //Obsolete
#define RCS_RED_GET_DC_INFO                     0x0206
#define RCS_RED_SEND_DC_INFO                    0x0207
#define RCS_RED_GET_DC_PACKET                   0x0208  //Obsolete
#define RCS_RED_SEND_DC_PACKET                  0x0209
#define RCS_RED_SEND_DC_COMPLETE                0x020A
#define RCS_RED_SEND_MOUSE_EVENT                0x020B
#define RCS_RED_READ_MOUSE_EVENT                0x020C
#define RCS_RED_GET_TEXT_SCREEN_LOG             0x020D  //Obsolete
#define RCS_RED_SERVICE_DYRM                    0x020E
#define RCS_RED_SERVICE_IDRY                    0x020F
/************No of funtions beginning with 020**********************/
#define FNS_020 16
/*******************************************************************/



/*******************************GROUP 021**************************/
#define RCS_RED_GET_POST_LOG                    0x0210
#define RCS_RED_GET_CRASH_SCREEN                0x0211
#define RCS_RED_PASSTHRU_DATA_TO_HOST           0x0212
#define RCS_RED_PASSTHRU_DATA_TO_REMOTE         0x0213
#define RCS_RED_READ_PASSTHRU_DATA              0x0214
#define RCS_RED_REPORT_CURSOR_POS               0x0215
/************No of funtions beginning with 021**********************/
#define FNS_021 6
/*******************************************************************/


/*******************************GROUP 022**************************/
/************* This Group has Remote Floppy Command ***************/
# define RCS_RED_START_REMOTE_FLOPPY				0x0220
# define RCS_RED_STOP_REMOTE_FLOPPY					0x0221
# define RCS_RED_INT_13H_COMMAND					0x0222
# define RCS_RED_REMOTE_FLOPPY_COMMAND				0x0223
# define RCS_RED_REMOTE_FLOPPY_STATUS				0x0224


/************No of funtions beginning with 022**********************/
#define FNS_022 5
/*******************************************************************/



/*******************************GROUP 023**************************/

/************No of funtions beginning with 023**********************/
#define FNS_023 0
/*******************************************************************/



/*******************************GROUP 024**************************/

/************No of funtions beginning with 024**********************/
#define FNS_024 0
/*******************************************************************/



/*******************************GROUP 025**************************/

/************No of funtions beginning with 025**********************/
#define FNS_025 0
/*******************************************************************/


/*******************************GROUP 026**************************/

/************No of funtions beginning with 026**********************/
#define FNS_026 0
/*******************************************************************/



/*******************************GROUP 027**************************/

/************No of funtions beginning with 027**********************/
#define FNS_027 0
/*******************************************************************/



/*******************************GROUP 028**************************/

/************No of funtions beginning with 028**********************/
#define FNS_028 0
/*******************************************************************/



/*******************************GROUP 029**************************/

/************No of funtions beginning with 029**********************/
#define FNS_029 0
/*******************************************************************/


/*******************************GROUP 02A**************************/

/************No of funtions beginning with 02A**********************/
#define FNS_02A 0
/*******************************************************************/



/*******************************GROUP 02B**************************/

/************No of funtions beginning with 02B**********************/
#define FNS_02B 0
/*******************************************************************/


/*******************************GROUP 02C**************************/

/************No of funtions beginning with 02C**********************/
#define FNS_02C 0
/*******************************************************************/




/*******************************GROUP 02D**************************/

/************No of funtions beginning with 02D**********************/
#define FNS_02D 0
/*******************************************************************/



/*******************************GROUP 02E**************************/

/************No of funtions beginning with 02E**********************/
#define FNS_02E 0
/*******************************************************************/



/*******************************GROUP 02F**************************/

/************No of funtions beginning with 02F**********************/
#define FNS_02F 0
/*******************************************************************/




/************************************************************************/
/**************  REMOTE CONSOLE REDIRECTION COMMANDS END*****************/
/************************************************************************/

/************************************************************************/
/**************  REMOTE CONSOLE REDIRECTION STRUCTURES  *****************/
/************************************************************************/

typedef struct _CrashScreenLogEntryHeaderTAG
{
    DATE_TIME   DateTimeStamp;
    u8        Rows, Columns;
    u8        CursorPosRow;
    u8        CursorPosCol;
    u8        Reserved [4];
} CRASH_SCREEN_LOG_ENTRY_HEADER;

typedef struct _CrashScreenLogEntryTAG
{
    CRASH_SCREEN_LOG_ENTRY_HEADER   CrashScreenHeader;
    u8    CrashScreenBuffer [16000];
} CRASH_SCREEN_LOG_ENTRY;

typedef struct _RCS_FN_GetCrashScreenArgsTag
{
   CRASH_SCREEN_LOG_ENTRY CrashScreen;
}RCS_FN_GET_CRASH_SCREEN_ARGS;

typedef struct _RCS_FN_GetCrashScreenCommandTag
{
    RCS_COMMAND_PACKET RCSCmdPkt;
    RCS_FN_GET_CRASH_SCREEN_ARGS Data;
} RCS_FN_GET_CRASH_SCREEN_COMMAND;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
} RCS_FN_START_CONSOLE_REDIRECTION_COMMAND;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
} RCS_FN_STOP_CONSOLE_REDIRECTION_COMMAND;

typedef struct
{
    u8    VideoMode;
} VIDEO_MODE_ARGS;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    VIDEO_MODE_ARGS     Data;
} RCS_FN_SEND_VIDEO_MODE_COMMAND;

typedef struct _KEY_DATA
{
    u8    KeySync;
    u8    ScanCode;
    u8    ASCIICode;
    u8    RAWData [2];
} KeyData;

typedef struct
{
    KeyData KeyCodes [1];           //Variable Length Command
} SEND_KEY_ARGS;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    SEND_KEY_ARGS   Data;
} RCS_FN_SEND_KEY_COMMAND;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
} RCS_FN_READ_KEY_COMMAND;          //Obsolete Command

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
} RCS_FN_GET_DC_INFO_COMMAND;

typedef struct
{
    u8    RawData [1];            //Variable Length Command
} SEND_DC_INFO_ARGS;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    SEND_DC_INFO_ARGS   Data;
} RCS_FN_SEND_DC_INFO_COMMAND;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
} RCS_FN_GET_DC_PACKET_COMMAND;     //Obsolete Command

typedef struct
{
    u8    RawData [1];            //Variable Length Command
} SEND_DC_PACKET_ARGS;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    SEND_DC_PACKET_ARGS   Data;
} RCS_FN_SEND_DC_PACKET_COMMAND;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
} RCS_FN_SEND_DC_COMPLETE_COMMAND;

typedef struct
{
    u8    RawData [1];            //Variable Length Command
} MOUSE_EVENT_ARGS;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    MOUSE_EVENT_ARGS   Data;
} RCS_FN_SEND_MOUSE_EVENT_COMMAND;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    MOUSE_EVENT_ARGS   Data;
} RCS_FN_READ_MOUSE_EVENT_COMMAND;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
} RCS_FN_GET_TEST_SCREEN_LOG_COMMAND;   //Obsolete Command

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
} RCS_FN_SERVICE_DYRM_COMMAND;

typedef struct
{
    u8    RawData [1];            //Variable Length Command
} SERVICE_IDRY_ARGS;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    SERVICE_IDRY_ARGS   Data;
} RCS_FN_SERVICE_IDRY_COMMAND;

/************************************************************************/
/**************  REMOTE CONSOLE REDIRECTION STRUCTURES  END  ************/
/************************************************************************/
#endif

