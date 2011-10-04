
/*******************************************************************

    File Name   :   EVTLOG.H

    Author      :   K.V.Subash & Parthiban Baskar

    Date        :   2 December 1997

    Purpose     :   Prototype shell code for the Event Logger
                    Contains structures used internally by
                    the Event Logger and can be included
                    by other modules which write to the
                    Event Que

	Copyright	:	American Megatrends Inc., 1997-1998

********************************************************************/

#ifndef __EVTLOG_H__
#define __EVTLOG_H__

#include "dtypes.h"
#include "rtc.h"

typedef struct _EventPacket
{
	FOURBYTES EventCode;
	u8 EventData[100];
	u8 OtherData[16];
}
EVENT_PACKET;




typedef struct _Event_Log_Entry
{
	DATE_TIME DateTime;
	FOURBYTES EventCode;
	u8 Reserved[16];
	u8 EventData[100];
}
EVENT_LOG_ENTRY;


extern int EVTLOG_GetMostRecentNEvents(unsigned short* NoOfEvents,unsigned char* EventBuffer);

#endif








