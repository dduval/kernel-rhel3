#ifndef  __MISC_H__
#define  __MISC_H__

#include "rcs.h"
#include "dtypes.h"

#define NUM_GENERIC_DATA   4
#define MAX_GENERIC_DATA   (1024*4)

#define AR_DISABLE      0X00
#define AR_HW_RESET     0X01
#define AR_HW_PWRCYCLE  0X02
#define MINIMUM_AUTO_RECOVERY_TIMER  15

typedef struct GET_VERSION_TYPE_tag
{
	u8 MajorNum;
	u8 MinorNum;
	u8 BuildDate[12];
	u8 Prefix[4];
}GET_VERSION_TYPE;

typedef struct GET_MEGARAC_INFO_TYPE_tag
{
   u8 Processor[30];
   u8 CPUSpeed[8];
   u8 MemorySize[8];
   u8 ROMSize[8];
   u8 NVRAMSize[8];
   u8 HostBridge[30];
   u8 NetworkController[30];
   u8 ModemType;
   u8 PCMCIAController[30];
   u8 PcCardManufacturer[30];
   u8 PcCardProductName[30];
   u8 BatteryInfo[30];
   TWOBYTES Series;
   u8 Reserved[14];
   u8 NICAddr[6];
}GET_MEGARAC_INFO_TYPE;

typedef struct MISC_CFG_TYPE_tag
{
	TWOBYTES StartupTimeDelay;
	u8 AutoRecovery;
	TWOBYTES HeartBeatTimeout;
	u8 ServerID;
}MISC_CFG_TYPE;

typedef struct MISC_CFG_CMD_tag
{
	RCS_COMMAND_PACKET header;
	MISC_CFG_TYPE info;
}MISC_CFG_CMD;

typedef struct _IPMI_GetSELInfoResp{
	unsigned char	Version;
	unsigned char 	NoEntriesLSB;
	unsigned char	NoEntriesMSB;
    unsigned char   FreespaceLSB;
    unsigned char   FreespaceMSB;
	unsigned char 	AddTime[4];
	unsigned char 	EraseTime[4];
	unsigned char	Operation;
}IPMI_GET_SEL_INFO_RESP;

typedef struct _IPMI_PowerCmd{
	unsigned char Power;
}IPMI_POWER_CMD;

typedef struct _GetIPMIInfoArgs
{
   unsigned char  DestAddr;     /* Input Only . Unaltered on Return */
   unsigned char  DestLun;      /* Input Only . Unaltered on Return */
   unsigned char  NetFn;        /* Input Only . Unaltered on Return */
   unsigned char  Cmd;          /* Input Only . Unaltered on Return */
   unsigned char  RetCode;      /* Output - IPMI Return Code        */
   int            BufLen;       /* Input Only . IPMIData Max Size   */
   int            DataLen;      /* Input & Output - Data in IPMIData*/
   char           IPMIData[1];  /* Input & Output - IPMI Data       */    
}GET_IPMI_INFO_ARGS;

typedef struct AUTO_RECOVERY_CFG_TYPE_tag
{
   u8 AutoRecovery;
   FOURBYTES AutoRecoveryTimer;
   u8 Reserved[11];
}AUTO_RECOVERY_CFG_TYPE;

#endif
