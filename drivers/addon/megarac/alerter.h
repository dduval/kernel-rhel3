
/*******************************************************************

	File Name    :   ALERTER.H

	Author       :   K.V.Subash & Parthiban Baskar

	Date         :   4 December 1997

	Purpose      :   Prototype shell code for the Alerter
					 Contains structures used internally by
					 the Alerter and can be included

					 by other modules which write to the
					 Alerter Que(mostly Watcher)

	Copyright    :   American Megatrends Inc., 1997-1998

********************************************************************/
#ifndef __ALERTER_H__
#define __ALERTER_H__

#include "dtypes.h"
#include "rtc.h"

#define PAGER_TYPE_NONE 00
#define PAGER_TYPE_NUMERIC 01
#define PAGER_TYPE_TAP 02

#define ALERT_TYPES_NO_ALERT                    0x00
#define ALERT_TYPES_MANAGED_CONSOLE   0x01
#define ALERT_TYPES_PAGE                              0x02
#define ALERT_TYPES_SNMPTRAP                    0x03
//r
#define ALERT_TYPES_EMAIL                    0x04
//r

#define LENGTH_OF_ALERT_DATA           100

#define MAX_NO_ALERT_METHODES 4
typedef struct _AlerterPacket
{
	FOURBYTES AlertCode;
	u8 AlertData[100];
	u8 OtherData[16];
}
ALERTER_PACKET;

typedef struct _AlertPacket
{
	DATE_TIME DateTime;
	FOURBYTES AlertCode;
	u8 Reserved[16];
	u8 AlertData[100];
}
ALERT_PACKET;



typedef struct AlertOrderTable
{
u8 AlertMethod[4];
u8 ReservedForExtraMethods[4];
u8 RetriesMethod[4];
u8 ReservedForExtraMethodRetries[4];
TWOBYTES NotificationFrequency;
}
ALERT_ORDER_TABLE;

//the alert order tabel structure which is used to maintain order of alerting
//info. This is different from the individual admins alert info. He may choose to be paged or 
//otherwise

extern int ALERTER_ReadAlertTable(void);
extern int ALERTER_ModifyAlertTable(void);
#endif

