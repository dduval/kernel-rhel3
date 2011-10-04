#ifndef __COMRCS_H__
#define __COMRCS_H__

#include "rcs.h"
#include "dtypes.h"

#define SNMP_ENTRIES		8
#define MGNT_CON_ENTRIES	8

#define LENGTH_OF_PHONENUMBER 33
#define LENGTH_OF_EXTRA_MODEM_STRING 80
#define LENGTH_OF_COMUNITY_NAME 64

#define LENGTH_OF_DIALOUT_ALIAS 32
#define LENGTH_OF_DIALOUT_PASSWORD 32

typedef struct NET_CFG_TYPE_tag
{
	FOURBYTES	IP;
	FOURBYTES	SubnetMask;
	FOURBYTES   Gateway;
}NET_CFG_TYPE;

typedef struct PPP_CFG_TYPE_tag
{
	FOURBYTES	IPPoolStart;
}PPP_CFG_TYPE;

typedef struct DIALOUT_CFG_TYPE_tag
{
   char  Alias [LENGTH_OF_DIALOUT_ALIAS];       // Dial out Alias
   char  Password [LENGTH_OF_DIALOUT_PASSWORD]; // Dial out Password
}DIALOUT_CFG_TYPE;

typedef struct MODEM_CFG_TYPE_tag
{
	u8		ModemSelect;
	FOURBYTES   BaudRate;
	u8		DialMode;
	u8		ExtraModemInitString[LENGTH_OF_EXTRA_MODEM_STRING];
	FOURBYTES	PowerOnDelay;
	FOURBYTES	SignalDelay;
	FOURBYTES	RingDelay;
	FOURBYTES	CDDelay;
	FOURBYTES	ResponseDelay;
	FOURBYTES	HangupDelay;
	FOURBYTES	ConnectTimeOut;
	FOURBYTES	DetectTimeOut;
}MODEM_CFG_TYPE;

typedef struct SNMP_DEST_TYPE_tag
{
        u8        index;
	FOURBYTES   IP;
        u8        ComunityName[LENGTH_OF_COMUNITY_NAME];
}SNMP_DEST_TYPE;



typedef struct MGNT_CON_TYPE_tag
{
	u8	index;
	u8	PhoneNumber[LENGTH_OF_PHONENUMBER];
}MGNT_CON_TYPE;

typedef struct TCPIP_CFG_TYPE_tag{
   TWOBYTES wEtherMTU;
   TWOBYTES wTCPTTL;
   TWOBYTES wSRTTBase;
   TWOBYTES wSRTTDefault;
   TWOBYTES wTCPReXmtMin;
   TWOBYTES wTCPReXmtMax;
//   TWOBYTES wTCPMSL;          /* max seg lifetime */
//   TWOBYTES wTCPPersistMin    /* retransmit persistance */
//   TWOBYTES wTCPPersistMax    /* maximum persist interval */
//   TWOBYTES wTCPKeepInit      /* initial connect keep alive */
//   TWOBYTES wTCPKeepIdle      /* dflt time before probing */
//   TWOBYTES wTCPKeepInterval  /* default probe interval */
//   TWOBYTES wTCPKeepCount     /* max probes before drop */
//   TWOBYTES wTCPLingerTime    /* linger at most 2 minutes */
//   TWOBYTES wTCPMaxReXmt      /* maximum retransmits */
   u8 Reserved[52];
}TCPIP_CFG_TYPE;

#endif

