/*******************************************************************

    File Name   :   RCS.H

    Author      :   K.V.Subash & Parthiban Baskar

    Date        :   8 December 1997

    Purpose     :   Data types and structures used by the RCS

	Copyright	:	American Megatrends Inc., 1997-1998

********************************************************************/
#ifndef __RCS_H__
#define __RCS_H__

#include "dtypes.h"


/****************RCS COMMAND GROUPS****************/
#define RCS_CMD                          0x0000
#define RCS_CONN                         0x0100
#define RCS_RED                          0x0200
#define RCS_ADMIN                        0x0400
#define RCS_DIAG                         0x0800
#define RCS_USR                          0x8000
/**************************************************/

/********************RCS CHANNELS*******************/
#define RCS_CHANNEL_HOST 0
#define RCS_CHANNEL_REMOTE 1
#define RCS_CHANNEL_SELF_INITIATED  2
/**************************************************/


typedef struct _RCSCommandPacket
{
    union
    {
        TWOBYTES FullCmd;
        struct
        {
            u8 CmdCode;
            u8 CmdGrp;
        } CmdBits;
    } Cmd;
    union
    {
        TWOBYTES FullStatus;
        struct
        {
            u8 StatusCode;
            u8 StatusGrp;
        } StatusBits;
     } Status;

     TWOBYTES Length;
     u8   Channel;
     u8   Entry;
     FOURBYTES Handle;
     u8 Reserved[4];
}
RCS_COMMAND_PACKET;


#endif
























