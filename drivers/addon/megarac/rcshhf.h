//
//  Definition file for Hhf structure
//

#ifndef __RCSHHF_H__
#define __RCSHHF_H__

typedef struct GET_NUM_CATEGORY_TYPE_tag
{
	u8 CatNum;
}GET_NUM_CATEGORY_TYPE;

typedef struct NO_OF_HHF_CAT_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	GET_NUM_CATEGORY_TYPE info;
}NO_OF_HHF_CAT_PACKET;

typedef struct GET_CATEGORY_NAME_TYPE_tag
{
	u8 CatNum;
	char CatName[32];
}GET_CATEGORY_NAME_TYPE;

typedef struct HHF_CAT_NAME_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	GET_CATEGORY_NAME_TYPE info;
}HHF_CAT_NAME_PACKET;

typedef struct GET_NUM_SUBDIVISIONS_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
}GET_NUM_SUBDIVISIONS_TYPE;

typedef struct NO_OF_HHF_SUBDIV_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	GET_NUM_SUBDIVISIONS_TYPE info;
}NO_OF_HHF_SUBDIV_PACKET;

typedef struct GET_SUBDIV_NAME_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	char SubName[32];
}GET_SUBDIV_NAME_TYPE;

typedef struct HHF_SUBDIV_NAME_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	GET_SUBDIV_NAME_TYPE info;
}HHF_SUBDIV_NAME_PACKET;

typedef struct GET_HHF_VALUE_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	u8 HostID;
	u8 ID;
	u8 HhfValue[32];
	u8 HhfUnit[16];
}GET_HHF_VALUE_TYPE;

typedef struct HHF_VALUE_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	GET_HHF_VALUE_TYPE info;
}HHF_VALUE_PACKET;

typedef struct EN_DIS_HHF_MONITOR_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	u8 Mode;
}EN_DIS_HHF_MONITOR_TYPE;

typedef struct ENABLE_HHF_MONITOR_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	EN_DIS_HHF_MONITOR_TYPE info;
}ENABLE_HHF_MONITOR_PACKET;

typedef struct EN_DIS_HHF_THRES_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	u8 Mode;
}EN_DIS_HHF_THRES_TYPE;

typedef struct ENABLE_HHF_THRESHOLD_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	EN_DIS_HHF_THRES_TYPE info;
}ENABLE_HHF_THRESHOLD_PACKET;

typedef struct HHF_THRES_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	u8 LowAlertLimit[32];
	u8 HighAlertLimit[32];
	u8 LowWarnLimit[32];
	u8 HighWarnLimit[32];
}HHF_THRES_TYPE;

typedef struct READ_HHF_THRESHOLD_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	HHF_THRES_TYPE info;
}READ_HHF_THRESHOLD_PACKET;

typedef struct MODIFY_HHF_THRESHOLD_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	HHF_THRES_TYPE info;
}MODIFY_HHF_THRESHOLD_PACKET;

typedef struct HHF_SCHED_TYPE_tag
{
	u8 CatNum;
	u8 SubNum;
	unsigned short Attributes;
	unsigned long Period;
}HHF_SCHED_TYPE;

typedef struct READ_HHF_SCHEDULE_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	HHF_SCHED_TYPE info;
}READ_HHF_SCHEDULE_PACKET;

typedef struct MODIFY_HHF_SCHEDULE_PACKET_tag
{
	RCS_COMMAND_PACKET header;
	HHF_SCHED_TYPE info;
}MODIFY_HHF_SCHEDULE_PACKET;


#endif

