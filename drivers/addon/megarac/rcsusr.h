/*******************************************************************

    File Name   :   RCSUSR.H

    Author      :   K.V.Subash & Parthiban Baskar

    Date        :   8 December 1997

    Purpose     :   RCS User Defined Cmd Grp Codes

	Copyright       :       American Megatrends Inc., 1997-1998

********************************************************************/

#ifndef __RCSUSR_H__
#define __RCSUSR_H__


/************************************************************************/
/**************  REMOTE USER DEFINED COMMANDS ***************************/
/************************************************************************/

/*******************************GROUP 800**************************/
#define RCS_USR_GET_ESMII_INFO                  0X8000
#define RCS_USR_GET_ESMII_EVENT_LOG             0X8001
#define RCS_USR_GET_ESMII_POST_LOG              0X8002
#define RCS_USR_SCAN_FOR_ESMIIS                 0X8003
#define RCS_USR_SEND_ESMII_COMMAND              0X8004
#define RCS_USR_READ_ESMII_CONFIG_COMMAND       0X8005
#define RCS_USR_MODIFY_ESMII_CONFIG_COMMAND     0X8006
#define RCS_USR_DUMP_ESMII_EVENT_LOG            0X8007
#define RCS_USR_DUMP_ESMII_POST_LOG             0X8008
#define RCS_USR_GET_NUM_ESMIIS                  0X8009
#define RCS_USR_GET_SERVER_INFO                 0X800A
#define RCS_USR_CLEAR_ESMII_EVENT_LOG           0X800B
#define RCS_USR_CLEAR_ESMII_POST_LOG            0X800C
#define RCS_USR_GET_GLOBAL_ALERT_SEVERITY       0X800D
#define RCS_USR_REPORT_SERVER_INFO              0X800E
/************No of functions beginning with 800**********************/
#define FNS_800 15
/*******************************************************************/


/*******************************GROUP 801**************************/
#define RCS_USR_REPORT_HOST_INFO                0X8010
#define RCS_USR_GET_HOST_INFO                   0X8011
#define RCS_USR_GET_CURRENT_ESMII_POST_LOG      0X8012
#define RCS_USR_GET_ALL_ESMII_POST_LOGS         0X8013
#define RCS_USR_CLEAR_ALL_ESMII_POST_LOGS       0X8014
/************No of functions beginning with 801**********************/
#define FNS_801 5
/*******************************************************************/



/*******************************GROUP 802**************************/
#define RCS_USR_SEND_IM_COMMAND                 0x8020

#define RCS_USR_READ_IM_CONFIG_COMMAND          0X8021
#define RCS_USR_MODIFY_IM_CONFIG_COMMAND        0X8022

#define RCS_USR_READ_IM_PROC_COMMAND            0x8023
#define RCS_USR_MODIFY_IM_PROC_COMMAND          0x8024

#define RCS_USR_READ_IM_SYSVOLT_COMMAND         0x8025
#define RCS_USR_MODIFY_IM_SYSVOLT_COMMAND       0x8026

#define RCS_USR_READ_IM_SYSTEMP_COMMAND         0x8027
#define RCS_USR_MODIFY_IM_SYSTEMP_COMMAND       0x8028

#define RCS_USR_READ_IM_SYSFAN_COMMAND          0x8029
#define RCS_USR_MODIFY_IM_SYSFAN_COMMAND        0x802A

#define RCS_USR_READ_IM_SYSFAULT_COMMAND        0x802B
#define RCS_USR_MODIFY_IM_SYSFAULT_COMMAND      0x802C

#define RCS_USR_READ_IM_SYSSWITCH_COMMAND       0x802D
#define RCS_USR_MODIFY_IM_SYSSWITCH_COMMAND     0x802E
/************No of functions beginning with 802**********************/
#define FNS_802 13
/*******************************************************************/



/*******************************GROUP 803**************************/
/************No of functions beginning with 803**********************/
#define FNS_803 0
/*******************************************************************/



/*******************************GROUP 804**************************/
#define RCS_USR_IPMI_GET_EVT_LOG_COMMAND      0x8040
#define RCS_USR_IPMI_CLEAR_EVENT_LOG_COMMAND  0x8041
#define RCS_USR_GET_SDR_RECORD_COMMAND        0x8042
#define RCS_USR_GET_FRU_INFO_COMMAND		  0x8043
#define RCS_USR_RESCAN_FRU_COMMAND			  0x8044
#define	RCS_USR_START_STOP_IPMI_COMMAND		  0x8045
#define	RCS_USR_DUMP_SDR_RECORDS			  0x8046
/************No of functions beginning with 804**********************/
#define FNS_804 7
/*******************************************************************/



/*******************************GROUP 805**************************/

/************No of functions beginning with 805**********************/
#define FNS_805 0
/*******************************************************************/


/*******************************GROUP 806**************************/

/************No of functions beginning with 806**********************/
#define FNS_806 0
/*******************************************************************/



/*******************************GROUP 807**************************/

/************No of functions beginning with 807**********************/
#define FNS_807 0
/*******************************************************************/



/*******************************GROUP 808**************************/

/************No of functions beginning with 808**********************/
#define FNS_808 0
/*******************************************************************/



/*******************************GROUP 809**************************/

/************No of functions beginning with 809**********************/
#define FNS_809 0
/*******************************************************************/


/*******************************GROUP 80A**************************/

/************No of functions beginning with 80A**********************/
#define FNS_80A 0
/*******************************************************************/



/*******************************GROUP 80B**************************/

/************No of functions beginning with 80B**********************/
#define FNS_80B 0
/*******************************************************************/


/*******************************GROUP 80C**************************/

/************No of functions beginning with 80C**********************/
#define FNS_80C 0
/*******************************************************************/




/*******************************GROUP 80D**************************/

/************No of functions beginning with 80D**********************/
#define FNS_80D 0
/*******************************************************************/



/*******************************GROUP 80E**************************/

/************No of functions beginning with 80E**********************/
#define FNS_80E 0
/*******************************************************************/



/*******************************GROUP 80F**************************/

/************No of functions beginning with 80F**********************/
#define FNS_80F 0
/*******************************************************************/




/************************************************************************/
/**************  REMOTE USER DEFINED ERROR CODES ************************/
/************************************************************************/
#define RCS_USR_ERR_BUSY                        0x8003
#define RCS_USR_ERR_BAD_LENGTH                  0x8005
#define RCS_USR_ERR_BAD_ARGUMENT                0x8006
#define RCS_USR_ERR_DEVICE_NO_RESPONSE          0x8009
#define RCS_USR_ERR_INFORMATION_NOT_AVAILABLE   0x800f

/************************************************************************/
/**************  REMOTE USER DEFINED COMMANDS END************************/
/************************************************************************/

/* Implementation specific */
/* Dell ESM Support */

//#define  MAX_ESMII            16
#define  MAX_ESMII            1
//#define  MAX_EVENT_LOG_SIZE   1024
#define  MAX_EVENT_LOG_SIZE   1300
#define  MAX_POST_LOG_SIZE    256
#define MAX_ESM_POST_LOGS  4

//mjb 8/8/99
#define SYS_MGMT_SUPPORT_NONE    0
#define SYS_MGMT_SUPPORT_ESM     1
#define SYS_MGMT_SUPPORT_IPMI    2
//mjb 8/8/99

typedef struct _EsmIITableEntry
{
   unsigned char  LogAddr;
   unsigned char  Uid[8];
}ESMII_TABLE_ENTRY;

typedef struct _ServerInfoEntry
{
   unsigned char  EsmIILogAddr;
   unsigned char  SystemModel;
   unsigned char  BIOSVersion[4];
   unsigned char  BackPlaneFirmware[4];
   unsigned char  reserved1[12];
   unsigned char  EsmIIFirmware[2];
   unsigned char  ServiceTag[6];
   unsigned char  AssetTag[11];
// mjb 8/8/99
//   unsigned char  reserved2;
   unsigned char  SysMgmtSupportType; // 0=none, 1=ESM, 2=IPMI
// mjb 8/8/99
   unsigned char  PSPBFirmware[4];
   unsigned char  reserved3[12];
   unsigned char  ServerName[50];
}SERVER_INFO_ENTRY;


typedef struct _GetEsmIIInfoData
{
   unsigned char     RACLogAddr;
   unsigned char     RACUid[8];
   unsigned char     NumESMII;
   char              ServerName[50];
   ESMII_TABLE_ENTRY ESMIITable[MAX_ESMII];
}GET_ESMII_INFO_DATA;

typedef struct _RCS_USR_GetEsmIIInfoArgs
{
   unsigned char     RACLogAddr;
   unsigned char     RACUid[8];
   unsigned char     NumESMII;
   char              ServerName[50];
   ESMII_TABLE_ENTRY ESMIITable[1];
}RCS_USR_GET_ESMII_INFO_ARGS;

typedef struct _RCS_USR_GetEsmIIInfoCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_ESMII_INFO_ARGS Data;
}RCS_USR_GET_ESMII_INFO_COMMAND;

// Following based on ESM 2 Log Types document dated 1/7/98
typedef struct _EsmIILogEntry
{
   unsigned char  RecordType;
   unsigned char  Severity;
   unsigned char  Length;
   unsigned char  Body[23]; // Max body size is 23
}ESMII_LOG_ENTRY;

typedef struct _EsmIIEventLog
{
   unsigned short NumEntries;
   unsigned short LogSize;
   unsigned char  LogData[MAX_EVENT_LOG_SIZE];
}ESMII_EVENT_LOG;

typedef struct _EsmIIPostLog
{
   unsigned short NumEntries;
   unsigned short LogSize;
   unsigned char  LogData[MAX_POST_LOG_SIZE];
   void  *  pprev;
   void  *  pnext;
}ESMII_POST_LOG;

typedef struct _EsmIILog
{
   unsigned char     LogAddr;
   unsigned char     Uid[8];
   ESMII_EVENT_LOG   Event;
   ESMII_POST_LOG    Post;
}ESMII_LOG;

typedef struct _RCS_USR_GetEsmIIEventLogArgs
{
   unsigned char  LogAddr;
   unsigned short NumEntries;
   unsigned short LogSize;
   unsigned char  LogData[1];
}RCS_USR_GET_ESMII_EVENT_LOG_ARGS;

typedef struct _RCS_USR_GetEsmIIEventLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_ESMII_EVENT_LOG_ARGS Data;
}RCS_USR_GET_ESMII_EVENT_LOG_COMMAND;

typedef struct _RCS_USR_GetEsmIIPostLogArgs
{
   unsigned char  LogAddr;
   unsigned short NumEntries;
   unsigned short LogSize;
   unsigned char  LogData[1];
}RCS_USR_GET_ESMII_POST_LOG_ARGS;

typedef struct _RCS_USR_GetEsmIIPostLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_ESMII_POST_LOG_ARGS Data;
}RCS_USR_GET_ESMII_POST_LOG_COMMAND;

typedef struct _RCS_USR_ScanForGetEsmIIsArgs
{
   unsigned short NumberOfEsmIIs;
}RCS_USR_SCAN_FOR_ESMIIS_ARGS;

typedef struct _RCS_USR_ScanForGetEsmIIsCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_SCAN_FOR_ESMIIS_ARGS Data;
}RCS_USR_SCAN_FOR_ESMIIS_COMMAND;

#define RCS_USR_SEND_ESMII_CMD_MAX_RESP (1024 * 2) /* just a guess */

typedef struct _RCS_USR_SendCommandToEsmIIArgs
{
   unsigned char  ESMIILogAddr;
   unsigned char  RACLogAddr;
   unsigned short CmdRespPacketLen;
   unsigned char  CmdRespPacket[1];
}RCS_USR_SEND_COMMAND_TO_ESMII_ARGS;

typedef struct _RCS_USR_SendCommandToGetEsmIICommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_SEND_COMMAND_TO_ESMII_ARGS Data;
}RCS_USR_SEND_COMMAND_TO_ESMII_COMMAND;


typedef struct _RCS_USR_ConfigEsmIIArgs
{
   unsigned long  EsmIIPollPeriod;
   unsigned long  StartEsmIIMonTimeOut;
   unsigned char  DefaultLogicalAddress;
}RCS_USR_CONFIG_ESMII_ARGS;

typedef struct _RCS_USR_ConfigEsmIICommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_CONFIG_ESMII_ARGS Data;
}RCS_USR_CONFIG_ESMII_COMMAND;


typedef struct _RCS_USR_DumpEsmIIEventLogArgs
{
   unsigned char  LogAddr;
   unsigned short NumEntries;
   unsigned short LogSize;
   unsigned char  LogData[1];
}RCS_USR_DUMP_ESMII_EVENT_LOG_ARGS;

typedef struct _RCS_USR_DumpEsmIIEventLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_DUMP_ESMII_EVENT_LOG_ARGS Data;
}RCS_USR_DUMP_ESMII_EVENT_LOG_COMMAND;

typedef struct _RCS_USR_DumpEsmIIPostLogArgs
{
   unsigned char  LogAddr;
   unsigned short NumEntries;
   unsigned short LogSize;
   unsigned char  LogData[1];
}RCS_USR_DUMP_ESMII_POST_LOG_ARGS;

typedef struct _RCS_USR_DumpEsmIIPostLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_DUMP_ESMII_POST_LOG_ARGS Data;
}RCS_USR_DUMP_ESMII_POST_LOG_COMMAND;

typedef struct _RCS_USR_GetNumEsmIIsArgs
{
   unsigned short NumEsmIIs;
}RCS_USR_GET_NUM_ESMIIS_ARGS;

typedef struct _RCS_USR_GetNumEsmIIsCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_NUM_ESMIIS_ARGS Data;
}RCS_USR_GET_NUM_ESMIIS_COMMAND;

typedef struct _RCS_USR_GetServerInfoArgs
{
   unsigned char  EsmIILogAddr;
   unsigned char  SystemModel;
   unsigned char  BIOSVersion[4];
   unsigned char  BackPlaneFirmware[4];
   unsigned char  reserved1[12];
   unsigned char  EsmIIFirmware[2];
   unsigned char  ServiceTag[6];
   unsigned char  AssetTag[11];
// mjb 8/8/99
//   unsigned char  reserved2;
   unsigned char  SysMgmtSupportType; // 0=none, 1=ESM, 2=IPMI
// mjb 8/8/99
   unsigned char  PSPBFirmware[4];
   unsigned char  reserved3[12];
   unsigned char  ServerName[50];
}RCS_USR_GET_SERVER_INFO_ARGS;

typedef struct _RCS_USR_GetServerInfoCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_SERVER_INFO_ARGS Data;
}RCS_USR_GET_SERVER_INFO_COMMAND;

typedef struct _RCS_USR_ClearEsmIIEventLogArgs
{
   unsigned char  EsmIILogAddr;
}RCS_USR_CLEAR_ESMII_EVENT_LOG_ARGS;

typedef struct _RCS_USR_ClearEsmIIEventLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_CLEAR_ESMII_EVENT_LOG_ARGS Data;
}RCS_USR_CLEAR_ESMII_EVENT_LOG_COMMAND;

typedef struct _RCS_USR_ClearEsmIIPostLogArgs
{
   unsigned char  EsmIILogAddr;
}RCS_USR_CLEAR_ESMII_POST_LOG_ARGS;

typedef struct _RCS_USR_ClearEsmIIPostLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_CLEAR_ESMII_POST_LOG_ARGS Data;
}RCS_USR_CLEAR_ESMII_POST_LOG_COMMAND;


typedef struct _RCS_USR_GetGlobalAlertSeverityArgs
{
   unsigned char  EsmIILogAddr;
   unsigned char  GlobalAlertSeverity;
}RCS_USR_GET_GLOBAL_ALERT_SEVERITY_ARGS;

typedef struct _RCS_USR_GetGlobalAlertSeverityCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_GLOBAL_ALERT_SEVERITY_ARGS Data;
}RCS_USR_GET_GLOBAL_ALERT_SEVERITY_COMMAND;

typedef struct _RCS_USR_ReportServerInfoArgs
{
   unsigned char  EsmIILogAddr;
   unsigned char  SystemModel;
   unsigned char  BIOSVersion[4];
   unsigned char  BackPlaneFirmware[4];
   unsigned char  reserved1[12];
   unsigned char  EsmIIFirmware[2];
   unsigned char  ServiceTag[6];
   unsigned char  AssetTag[11];
// mjb 8/8/99
//   unsigned char  reserved2;
   unsigned char  SysMgmtSupportType; // 0=none, 1=ESM, 2=IPMI
// mjb 8/8/99
   unsigned char  PSPBFirmware[4];
   unsigned char  reserved3[12];
   unsigned char  ServerName[50];
}RCS_USR_REPORT_SERVER_INFO_ARGS;

typedef struct _RCS_USR_ReportServerInfoCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_REPORT_SERVER_INFO_ARGS Data;
}RCS_USR_REPORT_SERVER_INFO_COMMAND;

typedef struct _RCS_USR_ReportHostInfoArgs
{
   unsigned char  HostServerName[50];
}RCS_USR_REPORT_HOST_INFO_ARGS;

typedef struct _RCS_USR_ReportHostInfoCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_REPORT_HOST_INFO_ARGS Data;
}RCS_USR_REPORT_HOST_INFO_COMMAND;

typedef struct _RCS_USR_GetHostInfoArgs
{
   unsigned char  HostServerName[50];
}RCS_USR_GET_HOST_INFO_ARGS;

typedef struct _RCS_USR_GetHostInfoCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_HOST_INFO_ARGS Data;
}RCS_USR_GET_HOST_INFO_COMMAND;

typedef struct _RCS_USR_GetCurrentEsmIIPostLogArgs
{
   unsigned short NumEntries;
   unsigned char  LogData[1];
}RCS_USR_GET_CURRENT_ESMII_POST_LOG_ARGS;

typedef struct _RCS_USR_GetCurrentEsmIIPostLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_CURRENT_ESMII_POST_LOG_ARGS Data;
}RCS_USR_GET_CURRENT_ESMII_POST_LOG_COMMAND;

typedef struct _RCS_USR_GetAllEsmIIPostLogsCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   unsigned char Data[1];
}RCS_USR_GET_ALL_ESMII_POST_LOGS_COMMAND;

typedef struct _RCS_USR_GetAllEsmIIPostLogsArgs
{
   unsigned char Data[1];
}RCS_USR_GET_ALL_ESMII_POST_LOGS_ARGS;

typedef struct _RCS_USR_ALT_GetAllEsmIIPostLogsCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_GET_ALL_ESMII_POST_LOGS_ARGS Data;
}RCS_USR_ALT_GET_ALL_ESMII_POST_LOGS_COMMAND;

typedef struct _RCS_USR_ClearAllEsmIIPostLogsCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
}RCS_USR_CLEAR_ALL_ESMII_POST_LOGS_COMMAND;

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

/* Gateway InfoManager Suppoprt */

typedef struct _RCS_USR_SendCommandToImArgs
{
   unsigned char TargetAddr;
   unsigned char Cmd;
   unsigned char ImPacket[1];
}RCS_USR_SEND_COMMAND_TO_IM_ARGS;

typedef struct _RCS_USR_SendCommandToImRespArgs
{
   unsigned char TargetAddr;
   unsigned char Cmd;
   unsigned char ImPacket[1];
}RCS_USR_SEND_COMMAND_TO_IM_RESP_ARGS;

typedef struct _RCS_USR_SendCommandToImCmdArgs
{
   unsigned char ImPacket[1];
}RCS_USR_SEND_COMMAND_TO_IM_CMD_ARGS;

typedef struct _RCS_USR_SendCommandToImCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_SEND_COMMAND_TO_IM_ARGS Data;
}RCS_USR_SEND_COMMAND_TO_IM_COMMAND;

typedef struct _RCS_USR_ConfigImArgs
{
   unsigned char  ImMonitorFlags;
   unsigned int   ImPollPeriod;
}RCS_USR_CONFIG_IM_ARGS;

typedef struct _RCS_USR_ConfigImCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_CONFIG_IM_ARGS Data;
}RCS_USR_CONFIG_IM_COMMAND;

typedef struct _RCS_USR_MeasureImArgs
{
   unsigned char   ImMeasureIndex;
   unsigned char   ImMeasurePacket[1];
}RCS_USR_MEASURE_IM_ARGS;

typedef struct _RCS_USR_MeasureImCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_MEASURE_IM_ARGS Data;
}RCS_USR_MEASURE_IM_COMMAND;

typedef struct _RCS_USR_FlagsImArgs
{
   unsigned char ImFlagsPacket[1];
}RCS_USR_FLAGS_IM_ARGS;

typedef struct _RCS_USR_FlagsImCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_FLAGS_IM_ARGS Data;
}RCS_USR_FLAGS_IM_COMMAND;

typedef struct _RCS_USR_SwitchImArgs
{
   unsigned short Mask;
   unsigned short Switch;
}RCS_USR_SWITCH_IM_ARGS;

typedef struct _RCS_USR_SwitchImCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_USR_SWITCH_IM_ARGS Data;
}RCS_USR_SWITCH_IM_COMMAND;

/************************************************************************/

#define MAX_IPMI_EVENT_LOGS 500
#define MAX_SDR_LOGS 110
#define MAX_IPMI_EVENT_LOG_SIZE 16


typedef struct _IPMI_SELRecord
{
    unsigned char    RecIDLSB;
    unsigned char	 RecIDMSB;
    unsigned char    RecordType;
    unsigned char    TimeStamp[4];
    unsigned char    GenID[2];
    unsigned char    EvMRev;
    unsigned char    SensorType;
    unsigned char    SensorNo;
    unsigned char    EventType;
    unsigned char    Data[3];
}IPMI_SEL_RECORD;

typedef struct _IPMI_EventLog
{
	unsigned char LogData[MAX_IPMI_EVENT_LOG_SIZE];
}IPMI_EVENT_LOG;

typedef struct _RCS_USR_GetIPMIEventLogArgs{
    unsigned short NumEntries;
    unsigned char LogData[1];
}RCS_USR_GET_IPMI_EVENT_LOG_ARGS;

typedef struct _RCS_USR_GetIPMIEventLogCommand
{
    RCS_COMMAND_PACKET RCSCmdPkt;
    RCS_USR_GET_IPMI_EVENT_LOG_ARGS Data;
}RCS_USR_GET_IPMI_EVENT_LOG_COMMAND;

typedef struct RCS_USR_ClearIPMIEventLogArgs
{
	unsigned short    NumEntries;
	unsigned char    Status;
}RCS_USR_CLEAR_IPMI_EVENT_LOG_ARGS;

typedef struct _RCS_USR_ClearIPMIEventLogCommand
{
	RCS_COMMAND_PACKET RCSCmdPkt;
	RCS_USR_CLEAR_IPMI_EVENT_LOG_ARGS Data;
}RCS_USR_CLEAR_IPMI_EVENT_LOG_COMMAND;

#define MAX_SDR_RECORD_DATA   64

typedef struct _RCS_USR_SDRRecordArgs
{
	unsigned char OwnerID;
	unsigned char Lun;
	unsigned char SensorNo;
	unsigned char Length;
	unsigned char LogData[1];
}RCS_USR_SDR_RECORD_ARGS;

typedef struct _RCS_USR_SDRRecordCommand
{
    RCS_COMMAND_PACKET RCSCmdPkt;
    RCS_USR_SDR_RECORD_ARGS Data;
}RCS_USR_SDR_RECORD_COMMAND;

typedef struct _RCS_USR_GetFRUDataArgs
{
	unsigned char LogData[1];
}RCS_USR_GET_FRU_DATA_ARGS;

typedef struct _RCS_USR_GetFRUDataCommand
{
	RCS_COMMAND_PACKET RCSCmdPkt;
	unsigned char UseIPMI;
	RCS_USR_GET_FRU_DATA_ARGS Data;
}RCS_USR_GET_FRU_DATA_COMMAND;

typedef struct _RCS_USR_ALT_GetFRUDataArgs
{
	unsigned char UseIPMI;
	unsigned char LogData[1];
}RCS_USR_ALT_GET_FRU_DATA_ARGS;

typedef struct _RCS_USR_ALT_GetFRUDataCommand
{
	RCS_COMMAND_PACKET RCSCmdPkt;
	RCS_USR_ALT_GET_FRU_DATA_ARGS Data;
}RCS_USR_ALT_GET_FRU_DATA_COMMAND;

typedef struct _RCS_USR_RescanFRUDataCommand
{
	RCS_COMMAND_PACKET	RCSCmdPkt;
	unsigned char		Status;
}RCS_USR_RESCAN_FRU_DATA_COMMAND;

typedef struct _RCS_USR_ALT_RescanFRUDataArgs
{
	unsigned char		Status;
}RCS_USR_ALT_RESCAN_FRU_DATA_ARGS;

typedef struct _RCS_USR_ALT_RescanFRUDataCommand
{
	RCS_COMMAND_PACKET	         RCSCmdPkt;
   RCS_USR_ALT_RESCAN_FRU_DATA_ARGS  Data;
}RCS_USR_ALT_RESCAN_FRU_DATA_COMMAND;

typedef struct _RCS_USR_StartStopIPMIMonCommand
{
	RCS_COMMAND_PACKET RCSCmdPkt;
	unsigned char	Status;
}RCS_USR_START_STOP_IPMI_MON_COMMAND;

typedef struct _RCS_USR_ALT_StartStopIPMIMonArgs
{
	unsigned char	Status;
}RCS_USR_ALT_START_STOP_IPMI_MON_ARGS;

typedef struct _RCS_USR_ALT_StartStopIPMIMonCommand
{
	RCS_COMMAND_PACKET RCSCmdPkt;
	RCS_USR_ALT_START_STOP_IPMI_MON_ARGS Data;
}RCS_USR_ALT_START_STOP_IPMI_MON_COMMAND;

typedef struct _RCS_USR_DumpSDRRecordsCommand
{
	RCS_COMMAND_PACKET RCSCmdPkt;
	unsigned char Status;
}RCS_USR_DUMP_SDR_RECORDS_COMMAND;

typedef struct _RCS_USR_ALT_DumpSDRRecordsArgs
{
	unsigned char Status;
}RCS_USR_ALT_DUMP_SDR_RECORDS_ARGS;

typedef struct _RCS_USR_ALT_DumpSDRRecordsCommand
{
	RCS_COMMAND_PACKET                  RCSCmdPkt;
   RCS_USR_ALT_DUMP_SDR_RECORDS_ARGS   Data;
}RCS_USR_ALT_DUMP_SDR_RECORDS_COMMAND;

/************************************************************************/
#endif

