/*******************************************************************

    File Name   :   RCSIC.H

    Author      :   K.V.Subash & Parthiban Baskar

    Date        :   8 December 1997

    Purpose     :   RCS Info And Ctrl Cmd grp codes

    Copyright   :   American Megatrends Inc., 1997-1998

********************************************************************/

#ifndef __RCSIC_H__
#define __RCSIC_H__

#include "rtc.h"
/************************************************************************/
/*******  REMOTE CONSOLE INFORMATION AND CONTROL COMMANDS  **************/
/************************************************************************/


/*******************************GROUP 000**************************/
#define RCS_CMD_GET_VERSION                 0x0000
#define RCS_CMD_GET_MEGARAC_INFO            0x0001
#define RCS_CMD_GET_TOTAL_NO_OF_EVENTS      0x0002
#define RCS_CMD_MOST_RECENT_EVENT_ENTRIES   0x0003
#define RCS_CMD_MOST_OLD_EVENT_ENTRIES      0x0004
#define RCS_CMD_CLEAR_EVENT_LOG             0x0005
#define RCS_CMD_GET_CURRENT_POST_LOG        0x0006
#define RCS_CMD_GET_ALL_POST_LOGS           0x0007
#define RCS_CMD_CLEAR_ALL_POST_LOGS         0x0008
/************No of funtions beginning with 000**********************/
#define FNS_000 9
/*******************************************************************/




/*******************************GROUP 001**************************/

#define RCS_CMD_GET_DMI_INFO                 0x0010
#define RCS_CMD_GET_IPMI_INFO                0x0011
#define RCS_CMD_REPORT_DMI_INFO              0x0012
#define RCS_CMD_GET_OS_STATUS                0x0013
#define RCS_CMD_GET_HOST_STATUS              0x0014
#define RCS_CMD_GET_GENERIC_DATA             0x0015
#define RCS_CMD_REPORT_GENERIC_DATA          0x0016
/************No of funtions beginning with 001**********************/
#define FNS_001 7
/*******************************************************************/




/*******************************GROUP 002**************************/
#define RCS_CMD_BATT_STATUS              0x0020
/************No of funtions beginning with 002**********************/
#define FNS_002 1
/*******************************************************************/



/*******************************GROUP 003**************************/
#define RCS_CMD_GET_HHF_CATEGORIES          0X0030
#define RCS_CMD_GET_HHF_CATEGORY_NAME       0x0031
#define RCS_CMD_GET_HHF_SUBDIVISIONS        0x0032
#define RCS_CMD_GET_HHF_SUBDIVISION_NAME    0x0033
#define RCS_CMD_GET_HHF_VALUE               0x0034
#define RCS_CMD_GET_SDK_CATEGORIES          0X0035
#define RCS_CMD_GET_SDK_CATEGORY_NAME       0x0036
#define RCS_CMD_GET_SDK_SUBDIVISIONS        0x0037
#define RCS_CMD_GET_SDK_SUBDIVISION_NAME    0x0038
#define RCS_CMD_GET_SDK_VALUE               0x0039
#define RCS_CMD_READ_SDK_CONFIG             0x003A
#define RCS_CMD_MODIFY_SDK_CONFIG           0x003B
#define RCS_CMD_I2C_REPEATED_START_ACCESS   0x003C
/************No of funtions beginning with 003**********************/
#define FNS_003 13
/*******************************************************************/


/*******************************GROUP 004**************************/
#define RCS_CMD_SHUTDOWN_OS                  0X0040
#define RCS_CMD_RESET_HOST                   0X0041
#define RCS_CMD_POWERCYCLE_HOST              0X0042
#define RCS_CMD_POWER_ON_OFF                 0X0043
#define RCS_CMD_RESET_HOST_AFTER_DELAY       0X0044
#define RCS_CMD_POWERCYCLE_HOST_AFTER_DELAY  0X0045
#define RCS_CMD_POWER_ON_OFF_AFTER_DELAY     0X0046
#define RCS_CMD_REPORT_IO_BASE_ADDRESS       0X0047
/************No of funtions beginning with 004**********************/
#define FNS_004 8
/*******************************************************************/



/*******************************GROUP 005**************************/

/************No of funtions beginning with 005**********************/
#define FNS_005 0
/*******************************************************************/


/*******************************GROUP 006**************************/
#define RCS_CMD_LOG_EVENT                    0X0060
#define RCS_CMD_SEND_ALERT                   0X0061

/************No of funtions beginning with 006**********************/
#define FNS_006 2
/*******************************************************************/



/*******************************GROUP 007**************************/
#define RCS_CMD_START_HEARTBEAT              0X0070
#define RCS_CMD_STOP_HEARTBEAT               0X0071

/************No of funtions beginning with 007**********************/
#define FNS_007 2
/*******************************************************************/


/*******************************GROUP 008**************************/

/************No of funtions beginning with 008**********************/
#define FNS_008 0
/*******************************************************************/



/*******************************GROUP 009**************************/

/************No of funtions beginning with 009**********************/
#define FNS_009 0
/*******************************************************************/


/*******************************GROUP 00A**************************/

/************No of funtions beginning with 00A**********************/
#define FNS_00A 0
/*******************************************************************/



/*******************************GROUP 00B**************************/

/************No of funtions beginning with 00B**********************/
#define FNS_00B 0
/*******************************************************************/


/*******************************GROUP 00C**************************/

/************No of funtions beginning with 00C**********************/
#define FNS_00C 0
/*******************************************************************/




/*******************************GROUP 00D**************************/

/************No of funtions beginning with 00D**********************/
#define FNS_00D 0
/*******************************************************************/



/*******************************GROUP 00E**************************/

/************No of funtions beginning with 00E**********************/
#define FNS_00E 0
/*******************************************************************/



/*******************************GROUP 00F**************************/

/************No of funtions beginning with 00F**********************/
#define FNS_00F 0

/*******************************************************************/

#define MAX_POWER_OR_RESET_DELAY    1800







/************************************************************************/
/*******  REMOTE CONSOLE INFORMATION AND CONTROL COMMANDS END************/
/************************************************************************/


/************************************************************************/
/*******  REMOTE CONSOLE INFORMATION AND CONTROL STRUCTURES*****/
/************************************************************************/

//these structures define arguments to the various functions.
//when a RCS Command Packet is put in the RCS Q , It usually
//is appended with another argument structure for a specific function
//The CCB Handler sees that it is a RCS CCB .But since the total length
//along with the Arguments for the various RCS Functions is variable
//the length of the total packet is put in the CCB Header.It then writes
//that many bytes into the RCS Q.
//The RCS Dispatcher takes only the RCS COMMAND PACKET header
//and then branches to the neccessary function.
//Then the function knows how many arguments to read and reads them from
//the RCS Queue.


typedef struct _RCS_FN_GetMostRecentNEventsArgs
{
    TWOBYTES NoOfEvents;
    u8 EventBuffer[1];
    //the buffer to hold the events follows immediately after this.
    //so this is made of size 1 . Actually it can be as long as neccessary
}
RCS_FN_GET_MOST_RECENT_N_EVENTS_ARGS;

typedef struct _RCS_FN_GetMostRecentNEventsCommand
{
    RCS_COMMAND_PACKET RCSCmdPkt;
    TWOBYTES NoOfEvents;
    u8 EventBuffer[1];
}
RCS_FN_GET_MOST_RECENT_N_EVENTS_COMMAND;


typedef struct _RCS_FNGetMostOldNEventsArgs
{
    TWOBYTES NoOfEvents;
    u8 EventBuffer[1];
}
RCS_FN_GET_MOST_OLD_N_EVENTS_ARGS;

typedef struct _RCS_FNGetMostOldNEventsCommand
{
    RCS_COMMAND_PACKET RCSCmdPkt;
    TWOBYTES NoOfEvents;
    u8 EventBuffer[1];
}
RCS_FN_GET_MOST_OLD_N_EVENTS_COMMAND;


typedef struct _RCS_FN_GetTotalNoOfEventsArgs
{
    TWOBYTES NoOfEvents;
}
RCS_FN_GET_TOTAL_NO_OF_EVENTS_ARGS;

typedef struct _RCS_FN_GetTotalNoOfEventsCommand
{
    RCS_COMMAND_PACKET RCSCmdPkt;
    TWOBYTES NoOfEvents;
}
RCS_FN_GET_TOTAL_NO_OF_EVENTS_COMMAND;


typedef struct _RCS_FN_PowerOnOffAfterDelayCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   u8     Power;
   TWOBYTES Delay;
}
RCS_FN_POWER_ON_OFF_AFTER_DELAY_COMMAND;

// mjb 12-16-99
typedef struct _RCS_FN_PowerCycleHostAfterDelayArgs
{
   TWOBYTES Delay;      // Delay in seconds
}RCS_FN_POWERCYCLE_HOST_AFTER_DELAY_ARGS;

typedef struct _RCS_FN_PowerCycleHostAfterDelayCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_POWERCYCLE_HOST_AFTER_DELAY_ARGS Data;
}
RCS_FN_POWERCYCLE_HOST_AFTER_DELAY_COMMAND;

typedef struct _RCS_FN_ResetHostAfterDelayArgs
{
   TWOBYTES Delay;      // Delay in seconds
}RCS_FN_RESET_HOST_AFTER_DELAY_ARGS;

typedef struct _RCS_FN_ResetHostAfterDelayCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_RESET_HOST_AFTER_DELAY_ARGS Data;
}
RCS_FN_RESET_HOST_AFTER_DELAY_COMMAND;

//
// The following are alternative structures that can be used for the
// POWER_ON_OFF_AFTER_DELAY command.
//
typedef struct _RCS_FN_PowerOnOffWithDelayArgs
{
   u8     Power;      // 0 = Off, 1 = On
   TWOBYTES Delay;      // Delay in seconds
}RCS_FN_POWER_ON_OFF_WITH_DELAY_ARGS;

typedef struct _RCS_FN_PowerOnOffWithDelayCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_POWER_ON_OFF_WITH_DELAY_ARGS Data;
}
RCS_FN_POWER_ON_OFF_WITH_DELAY_COMMAND;

// end mjb 12-16-99

typedef struct _RCS_FN_GetDMIInfoArgs
{
   unsigned short NumStructs;
   unsigned char  BCDRevision;
   unsigned char  Reserved;
   unsigned short DMIDataLen;
   unsigned char  DMIData[1];
}RCS_FN_GET_DMI_INFO_ARGS;

typedef struct _RCS_FN_GetDMIInfoCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_GET_DMI_INFO_ARGS Data;
}RCS_FN_GET_DMI_INFO_COMMAND;

typedef struct _RCS_FN_GetIPMIInfoArgs
{
   unsigned char  DestAddr;     /* Input Only . Unaltered on Return */
   unsigned char  DestLun;      /* Input Only . Unaltered on Return */
   unsigned char  NetFn;        /* Input Only . Unaltered on Return */
   unsigned char  Cmd;          /* Input Only . Unaltered on Return */
   unsigned char  RetCode;      /* Output - IPMI Return Code        */
   int            BufLen;       /* Input Only . IPMIData Max Size   */
   int            DataLen;      /* Input & Output - Data in IPMIData*/
   char           IPMIData[1];  /* Input & Output - IPMI Data       */    
}RCS_FN_GET_IPMI_INFO_ARGS;

typedef struct _RCS_FN_GetIPMIInfoCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_GET_IPMI_INFO_ARGS Data;
}RCS_FN_GET_IPMI_INFO_COMMAND;


typedef struct _RCS_FN_GetOSStatusArgs
{
   unsigned short HostOSStatus;
}RCS_FN_GET_OS_STATUS_ARGS;

typedef struct _RCS_FN_GetOSStatusCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_GET_OS_STATUS_ARGS Data;
}RCS_FN_GET_OS_STATUS_COMMAND;

typedef struct _RCS_FN_GetHostStatusArgs
{
   unsigned short HostStatus;
}RCS_FN_GET_HOST_STATUS_ARGS;

typedef struct _RCS_FN_GetHostStatusCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_GET_HOST_STATUS_ARGS Data;
}RCS_FN_GET_HOST_STATUS_COMMAND;

typedef struct _RCS_FN_ReportIoBaseAddressArgs
{
   unsigned long IoBaseAddress;
}RCS_FN_REPORT_IO_BASE_ADDRESS_ARGS;

typedef struct _RCS_FN_ReportIoBaseAddressCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_REPORT_IO_BASE_ADDRESS_ARGS Data;
}RCS_FN_REPORT_IO_BASE_ADDRESS_COMMAND;

#define MAX_GENERIC_DATA_SIZE 4096

typedef struct _RCS_FN_ReportGenericDataArgs
{
   unsigned short Index;
   unsigned short Length;
   unsigned char  Data[1];
}RCS_FN_REPORT_GENERIC_DATA_ARGS;

typedef struct _RCS_FN_ReportGenericDataCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_REPORT_GENERIC_DATA_ARGS Data;
}RCS_FN_REPORT_GENERIC_DATA_COMMAND;

typedef struct _RCS_FN_GetGenericDataArgs
{
   unsigned short Index;
   unsigned short Length;
   unsigned char  Data[1];
}RCS_FN_GET_GENERIC_DATA_ARGS;

typedef struct _RCS_FN_GetGenericDataCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_GET_GENERIC_DATA_ARGS Data;
}RCS_FN_GET_GENERIC_DATA_COMMAND;

typedef struct _RCS_FN_GetCurrentPostLogArgs
{
   TWOBYTES NumOfCodes;
   u8     Log[1];
}RCS_FN_GET_CURRENT_POST_LOG_ARGS;

typedef struct _RCS_FN_GetCurrentPostLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_GET_CURRENT_POST_LOG_ARGS Data;
}RCS_FN_GET_CURRENT_POST_LOG_COMMAND;

typedef struct _RCS_FN_GetAllPostLogsCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   u8 Data[1];
}RCS_FN_GET_ALL_POST_LOGS_COMMAND;


typedef struct _RCS_FN_LogEventArgs
{
   FOURBYTES   EventCode;
   u8        EventData[100];
   u8        OtherData[16];
}RCS_FN_LOG_EVENT_ARGS;

typedef struct _RCS_FN_LogEventCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_LOG_EVENT_ARGS Data;
}RCS_FN_LOG_EVENT_COMMAND;

typedef struct _RCS_FN_SendAlertArgs
{
   FOURBYTES AlertCode;
   u8 AlertData[100];
   u8 OtherData[16];
}RCS_FN_SEND_ALERT_ARGS;

typedef struct _RCS_FN_SendAlertCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_SEND_ALERT_ARGS Data;
}RCS_FN_SEND_ALERT_COMMAND;


typedef struct _RCS_FN_StartHeartbeatArgs
{
   FOURBYTES   HeartBeatPeriod;
   u8        Reserved[12];
}RCS_FN_START_HEARTBEAT_ARGS;

typedef struct _RCS_FN_StartHeartbeatCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_FN_START_HEARTBEAT_ARGS Data;
}RCS_FN_START_HEARTBEAT_COMMAND;

typedef struct _RCS_FN_StopCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
}RCS_FN_STOP_HEARTBEAT_COMMAND;

typedef struct _RCS_FN_ClearAllPostLogsCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
}RCS_FN_CLEAR_ALL_POST_LOGS_COMMAND;

#endif
