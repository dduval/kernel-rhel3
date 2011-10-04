//
//  Definition file for sdk rcs structures
//

#ifndef __RCSSDK_H__
#define __RCSSDK_H__


#include "i2crsa.h"

typedef struct SDK_I2C_REPEATED_START_PACKET_tag
{
        RCS_COMMAND_PACKET header;
        I2C_REPEATED_START_WITH_STATUS_TYPE info;
}SDK_I2C_REPEATED_START_PACKET;



typedef struct SDK_GET_NUM_CATEGORY_TYPE_tag
{
	u8 CatNum;
}SDK_GET_NUM_CATEGORY_TYPE;

typedef struct NO_OF_SDK_CAT_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	SDK_GET_NUM_CATEGORY_TYPE info;
}NO_OF_SDK_CAT_PACKET;

typedef struct SDK_GET_CATEGORY_NAME_TYPE_tag
{
	u8 CatNum;
	char CatName[32];
}SDK_GET_CATEGORY_NAME_TYPE;

typedef struct SDK_CAT_NAME_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	SDK_GET_CATEGORY_NAME_TYPE info;
}SDK_CAT_NAME_PACKET;

typedef struct SDK_GET_NUM_SUBDIVISIONS_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
}SDK_GET_NUM_SUBDIVISIONS_TYPE;

typedef struct NO_OF_SDK_SUBDIV_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	SDK_GET_NUM_SUBDIVISIONS_TYPE info;
}NO_OF_SDK_SUBDIV_PACKET;

typedef struct SDK_GET_SUBDIV_NAME_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	char SubName[32];
}SDK_GET_SUBDIV_NAME_TYPE;

typedef struct SDK_SUBDIV_NAME_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	SDK_GET_SUBDIV_NAME_TYPE info;
}SDK_SUBDIV_NAME_PACKET;

typedef struct GET_SDK_VALUE_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	u8 HostID;
	u8 ID;
   	unsigned short 	wFlag;
    // Bit15: Event change notification 0=HHF enabled, 1=HHF disabled
    // Bit14: HHF change notification. 0=enable, 1=disable
    // Bit13: Monitor HHF 0=enable, 1=disable
   	long   		  	lValue;
   	long          	lLowAlertLimit;
   	long          	lHighAlertLimit;
   	long          	lLowWarningLimit;
   	long          	lHighWarningLimit;
    long			lRangeLowLimit;
    long			lRangeHighLimit;
   	long          	lLastValue;
   	unsigned short  wEvent;
   	char            bUnitDesc;
   	// From IPMI spec
   	// this is used for converting the lValue to string for external
   	// interface to the card
   	//				1 - degrees c
   	//				4 - volts
   	//				18 - RPM
   	//				100 - chassis intrusion. Not in IPMI spec?

}GET_SDK_VALUE_TYPE;

typedef struct SDK_VALUE_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	GET_SDK_VALUE_TYPE info;
}SDK_VALUE_PACKET;

typedef struct EN_DIS_SDK_MONITOR_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	u8 Mode;
}EN_DIS_SDK_MONITOR_TYPE;

typedef struct ENABLE_SDK_MONITOR_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	EN_DIS_SDK_MONITOR_TYPE info;
}ENABLE_SDK_MONITOR_PACKET;

typedef struct EN_DIS_SDK_THRES_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	u8 Mode;
}EN_DIS_SDK_THRES_TYPE;

typedef struct ENABLE_SDK_THRESHOLD_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	EN_DIS_SDK_THRES_TYPE info;
}ENABLE_SDK_THRESHOLD_PACKET;

typedef struct SDK_THRES_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	long LowAlertLimit;
	long HighAlertLimit;
	long LowWarnLimit;
	long HighWarnLimit;
}SDK_THRES_TYPE;

typedef struct READ_SDK_THRESHOLD_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	SDK_THRES_TYPE info;
}READ_SDK_THRESHOLD_PACKET;

typedef struct MODIFY_SDK_THRESHOLD_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	SDK_THRES_TYPE info;
}MODIFY_SDK_THRESHOLD_PACKET;

typedef struct SDK_SCHED_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	unsigned short Attributes;
	unsigned long Period;
}SDK_SCHED_TYPE;

typedef struct READ_SDK_SCHEDULE_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	SDK_SCHED_TYPE info;
}READ_SDK_SCHEDULE_PACKET;

typedef struct MODIFY_SDK_SCHEDULE_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	SDK_SCHED_TYPE info;
}MODIFY_SDK_SCHEDULE_PACKET;

typedef struct SDK_CONFIG_TYPE_tag
{
        u8 Config;
}SDK_CONFIG_TYPE;

typedef struct SDK_CONFIG_PACKET_tag
{
        RCS_COMMAND_PACKET header;
        SDK_CONFIG_TYPE info;
}SDK_CONFIG_PACKET;

typedef struct READ_SDK_MONITOR_STATE_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	EN_DIS_SDK_MONITOR_TYPE info;
}READ_SDK_MONITOR_STATE_PACKET;

typedef struct READ_SDK_THRESHOLD_STATE_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	EN_DIS_SDK_THRES_TYPE info;
}READ_SDK_THRESHOLD_STATE_PACKET;

#endif

