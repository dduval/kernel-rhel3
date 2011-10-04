#ifndef __RCSDBG_H__
#define __RCSDBG_H__

#include "rcs.h"
#include "rtc.h"
#include "dbglog.h"
/************************************************************************/
/**************  REMOTE DEBUG COMMANDS **********************************/
/************************************************************************/

/*******************************GROUP 100**************************/
#define RCS_DEBUG_SWITCH_VIDEO_PRINT                 0X1000
#define RCS_DEBUG_SET_VIDEO_DEBUG_MODE               0x1001
#define RCS_DEBUG_GET_LATEST_ERROR_LOG               0x1002
#define RCS_DEBUG_GET_ERROR_LOG_ENTRIES              0x1003
#define RCS_DEBUG_GET_CHECKPT_HEADER                 0x1004
#define RCS_DEBUG_CLEAR_CHECKPOINT_LOG               0x1005
#define RCS_DEBUG_GET_NO_OF_MODULES                  0x1006
#define RCS_DEBUG_GET_MODULE_NAMES                   0x1007
#define RCS_DEBUG_GET_DESC_STRINGS                   0x1008
#define RCS_DEBUG_SET_DBGPRINT_MASKS                 0x1009
#define RCS_DEBUG_CLEAR_VIDEO_SCREEN                 0x100a
                                                           
/************No of funtions beginning with 100**********************/
#define FNS_100 11
/*******************************************************************/



typedef struct _GETMODULENAMESARGS
{
RCS_COMMAND_PACKET CmdHeader;
char ModuleNames[MAX_MODULES][20];
}
GETMODULENAMESARGS;


typedef struct _GETDESCSTRINGSARGS
{
RCS_COMMAND_PACKET CmdHeader;
int ModuleNo; //input
int NoOfStrings; // given by firmware
char Strings[1];
}
GETDESCSTRINGSARGS;

typedef struct _DBGPRINTMASKS
{
RCS_COMMAND_PACKET CmdHeader;
char Masks[MAX_MODULES];
}
DBGPRINTMASKS;


typedef struct _RCS_DBG_SwitchVideoPrintArgs
{
   int   Mode;
}RCS_DBG_SWITCH_VIDEO_PRINT_ARGS;

typedef struct _RCS_DBG_SwitchVideoPrintCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_SWITCH_VIDEO_PRINT_ARGS Data;
}RCS_DBG_SWITCH_VIDEO_PRINT_COMMAND;

typedef struct _RCS_DBG_SetVideoDebugModeArgs
{
   int   Mode;
}RCS_DBG_SET_VIDEO_DEBUG_MODE_ARGS;

typedef struct _RCS_DBG_SetVideoDebugModeCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_SET_VIDEO_DEBUG_MODE_ARGS Data;
}RCS_DBG_SET_VIDEO_DEBUG_MODE_COMMAND;

typedef struct _RCS_DBG_GetLatestErrorLogArgs
{
   long NoOfEntriesReqd;
   unsigned long Buffer[1];
}RCS_DBG_GET_LATEST_ERROR_LOG_ARGS;

typedef struct _RCS_DBG_GetLatestErrorLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_GET_LATEST_ERROR_LOG_ARGS Data;
}RCS_DBG_GET_LATEST_ERROR_LOG_COMMAND;

typedef struct _RCS_DBG_GetErrorLogEntriesArgs
{
   long  StartFrom;
   long  NoOfEntriesReqd;
   unsigned long Buffer[1];
}RCS_DBG_GET_ERROR_LOG_ENTRIES_ARGS;

typedef struct _RCS_DBG_GetErrorLogEntriesCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_GET_ERROR_LOG_ENTRIES_ARGS Data;
}RCS_DBG_GET_ERROR_LOG_ENTRIES_COMMAND;

typedef struct _RCS_DBG_GetCheckptHeaderArgs
{
   unsigned long  head;
   unsigned long* LogTable;
   unsigned long  TotalNoOfElementsNow;
   unsigned long  MaximumNoOfElements;
   unsigned long  HeaderChecksum;
   unsigned long  Reserved1;
   unsigned long  Reserved2;
}RCS_DBG_GET_CHECKPT_HEADER_ARGS;

typedef struct _RCS_DBG_GetCheckptHeaderCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_GET_CHECKPT_HEADER_ARGS Data;
}RCS_DBG_GET_CHECKPT_HEADER_COMMAND;

typedef struct _RCS_DBG_ClearCheckpointLogCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
}RCS_DBG_CLEAR_CHECKPOINT_LOG_COMMAND;

typedef struct _RCS_DBG_GetNoOfModulesArgs
{
   int   NoOfModules;
}RCS_DBG_GET_NO_OF_MODULES_ARGS;

typedef struct _RCS_DBG_GetNoOfModulesCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_GET_NO_OF_MODULES_ARGS Data;
}RCS_DBG_GET_NO_OF_MODULES_COMMAND;

typedef struct _RCS_DBG_GetModuleNamesArgs
{
   char ModuleNames[MAX_MODULES][20];
}RCS_DBG_GET_MODULE_NAMES_ARGS;

typedef struct _RCS_DBG_GetModuleNamesCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_GET_MODULE_NAMES_ARGS Data;
}RCS_DBG_GET_MODULE_NAMES_COMMAND;

typedef struct _RCS_DBG_GetDescStringsArgs
{
   int ModuleNo;     //input
   int NoOfStrings;  // given by firmware
   char Strings[1];
}RCS_DBG_GET_DESC_STRINGS_ARGS;

typedef struct _RCS_DBG_GetDescStringsCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_GET_DESC_STRINGS_ARGS Data;
}RCS_DBG_GET_DESC_STRINGS_COMMAND;

typedef struct _RCS_DBG_SetDbgprintMasksArgs
{
   char Masks[MAX_MODULES];
}RCS_DBG_SET_DBGPRINT_MASKS_ARGS;

typedef struct _RCS_DBG_SetDbgprintMasksCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DBG_SET_DBGPRINT_MASKS_ARGS Data;
}RCS_DBG_SET_DBGPRINT_MASKS_COMMAND;

typedef struct _RCS_DBG_ClearVideoScreenCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
}RCS_DBG_CLEAR_VIDEO_SCREEN_COMMAND;

#endif
