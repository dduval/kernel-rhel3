/*******************************************************************

	File Name   :   CCBHDLR.H

	Author      :   K.V.Subash & Parthiban Baskar

	Date        :   2 December 1997

	Purpose     :   Prototype shell code for the CCB handler

	Copyright       :       American Megatrends Inc., 1997-1998

********************************************************************/

#ifndef __CCBHDLR_H__
#define __CCBHDLR_H__

#include "dtypes.h"

/*********************Codes needed for CCB Classification*****************/
#define HB_COMMAND 0x0001
#define EVENT_COMMAND 0X0002
#define ALERT_COMMAND 0x0003

// Codes 4 and 5 are reserved
#define TEST_COMMAND 0x0004
#define HOST_OS_SHUTDOWN_RESPONSE_COMMAND 0x0005

#define START_HB_COMMAND 0x0006
#define STOP_HB_COMMAND 0x0007

#define SET_DATE_TIME_COMMAND 0x0008
#define GET_DATE_TIME_COMMAND 0x0009

#define MIN_HEARTBEAT_PERIOD 5
/*************************************************************************/

#define SEGFP(PTR) (PTR & 0xffff0000)
#define OFFFP(PTR) (PTR & 0x0000ffff)
#define HOSTTOLOCALADDR(PTR) ( (SEGFP(PTR) >> 4 + OFFFP(PTR)) + 0x40000000 )
#define ABSOLUTE(SEG,OFF) (SEG >> 4 + OFF)



typedef struct _CCB_Header
{
	TWOBYTES Command; /**indicates CCB Type**/
	TWOBYTES Status;  /**Completion status of request**/
	TWOBYTES Length;  /**Indicates length of the body**/
	u8 Reserved[10]; /**resreved for future use**/
}
CCB_Header;


typedef struct _Test_Command_Packet
{
	TWOBYTES Command; /**indicates CCB Type**/
	TWOBYTES Status;  /**Completion status of request**/
	TWOBYTES Length;  /**Indicates length of the body**/
	u8 Reserved[10]; /**resreved for future use**/
    TWOBYTES Counter;
}
TEST_COMMAND_PACKET;


typedef struct _CCB_Start_HB_Cmd_Packet
{
   CCB_Header  CCBHeader;
   FOURBYTES   HeartBeatPeriod;
   char Data[1];
}CCB_START_HB_CMD_PACKET;

typedef struct _CCB_HB_Cmd_Packet
{
   CCB_Header  CCBHeader;
   char        Data[1];       // Used for referencing time & date data
}CCB_HB_CMD_PACKET;

typedef struct _CCB_Event_Cmd_Packet
{
   CCB_Header  CCBHeader;
   char        Data[1];       // Used for referencing event data
}CCB_EVENT_CMD_PACKET;

typedef struct _CCB_Alert_Cmd_Packet
{
   CCB_Header  CCBHeader;
   char        Data[1];       // Used for referencing alert data
}CCB_ALERT_CMD_PACKET;

/**This command control block structure is actually used by the host.
Pertinent Question :: WILL THE REMOTE SIDE WRAP A CCB IN SOME OTHER FORM
AND SEND IT ALONG????*/

/* Answer to the above: NO
   The remote API is strictly Remote Console Service (RCS) packets.
   A CCB packet sent by the remote would be invalid and an error 
   returned.
   Wrapping a CCB has no context.
*/

typedef struct _CCB_Set_Date_Time_Cmd_Packet
{
   CCB_Header  CCBHeader;
   char        Data[1];       // Used for referencing time & date data
}CCB_SET_DATE_TIME_CMD_PACKET;

typedef struct _CCB_Get_Date_Time_Cmd_Packet
{
   CCB_Header  CCBHeader;
   char        Data[1];       // Used for referencing time & date data
}CCB_GET_DATE_TIME_CMD_PACKET;

#endif







