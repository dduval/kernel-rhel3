/*******************************************************************

	File Name   :   RCSADMIN.H

	Author      :   K.V.Subash & Parthiban Baskar

	Date        :   8 December 1997

	Purpose     :   RCS ADMIN cmd grp cmd codes

	Copyright       :       American Megatrends Inc., 1997-1998

********************************************************************/

#ifndef __RCSADMIN_H__
#define __RCSADMIN_H__

#include "rcs.h"
#include "rtc.h"
#include "admin.h"
#include "comrcs.h"
/************************************************************************/
/**************  REMOTE ADMIN AND CONFIG COMMANDS ***********************/
/************************************************************************/

/*******************************GROUP 040**************************/
#define RCS_ADMIN_ENABLE_HHF_MONITOR            0X0400
#define RCS_ADMIN_ENABLE_HHF_THRESHOLD          0X0401
#define RCS_ADMIN_READ_HHF_THRESHOLD            0X0402
#define RCS_ADMIN_MOD_HHF_THRESHOLD             0X0403
#define RCS_ADMIN_READ_HHF_SCHEDULE             0X0404
#define RCS_ADMIN_MOD_HHF_SCHEDULE              0X0405
#define RCS_ADMIN_READ_HHF_MONITOR_STATE        0x0406
#define RCS_ADMIN_READ_HHF_THRESHOLD_STATE      0x0407
#define RCS_ADMIN_ENABLE_SDK_MONITOR            0X0408
#define RCS_ADMIN_ENABLE_SDK_THRESHOLD          0X0409
#define RCS_ADMIN_READ_SDK_THRESHOLD            0X040A
#define RCS_ADMIN_MOD_SDK_THRESHOLD             0X040B
#define RCS_ADMIN_READ_SDK_SCHEDULE             0X040C
#define RCS_ADMIN_MOD_SDK_SCHEDULE              0X040D
#define RCS_ADMIN_READ_SDK_MONITOR_STATE        0x040E
#define RCS_ADMIN_READ_SDK_THRESHOLD_STATE      0x040F
/************No of funtions beginning with 040**********************/
#define FNS_040 16
/*******************************************************************/


/*******************************GROUP 041**************************/
#define RCS_ADMIN_ADD_ADMIN_ENTRY               0X0410
#define RCS_ADMIN_READ_ADMIN_ENTRY              0X0411
#define RCS_ADMIN_DELETE_ADMIN_ENTRY            0X0412
#define RCS_ADMIN_MODIFY_ADMIN_ENTRY            0X0413
/************No of funtions beginning with 041**********************/
#define FNS_041 4
/*******************************************************************/


/*******************************GROUP 042**************************/
#define RCS_ADMIN_READ_NETWORK_CFG              0X0420
#define RCS_ADMIN_MODIFY_NETWORK_CFG            0X0421
#define RCS_ADMIN_READ_MODEM_CFG                0X0422
#define RCS_ADMIN_MODIFY_MODEM_CFG              0X0423
#define RCS_ADMIN_ADD_SNMP_DESTINATION          0X0424
#define RCS_ADMIN_READ_SNMP_DESTINATION         0X0425
#define RCS_ADMIN_MODIFY_SNMP_DESTINATION       0X0426
#define RCS_ADMIN_DELETE_SNMP_DESTINATION       0X0427
#define RCS_ADMIN_ADD_MANAGEMENT_CONSOLE        0X0428
#define RCS_ADMIN_READ_MANAGEMENT_CONSOLE       0X0429
#define RCS_ADMIN_MODIFY_MANAGEMENT_CONSOLE     0X042A
#define RCS_ADMIN_DELETE_MANAGEMENT_CONSOLE     0X042B
#define RCS_ADMIN_READ_ALERT_TABLE              0X042C
#define RCS_ADMIN_MODIFY_ALERT_TABLE            0X042D
#define RCS_ADMIN_FORCE_SAVE_CONFIG             0x042E
#define RCS_ADMIN_TEST_PAGE                     0x042F
/************No of funtions beginning with 042**********************/
#define FNS_042 16
/*******************************************************************/



/*******************************GROUP 043**************************/
#define RCS_ADMIN_READ_PPP_CFG                  0X0430
#define RCS_ADMIN_MODIFY_PPP_CFG                0X0431
#define RCS_ADMIN_READ_DIALOUT_CFG              0X0432
#define RCS_ADMIN_MODIFY_DIALOUT_CFG            0X0433
#define RCS_ADMIN_READ_TCPIP_CFG                0X0434
#define RCS_ADMIN_MODIFY_TCPIP_CFG              0X0435
#define RCS_ADMIN_READ_DHCP_CFG                 0X0436
#define RCS_ADMIN_MODIFY_DHCP_CFG               0X0437

/*r added to Read Admin Mail Id*/
#define RCS_ADMIN_READ_MAIL_ID                  0X0438
#define RCS_ADMIN_MODIFY_MAIL_ID                0X0439
#define RCS_ADMIN_READ_MAIL_SERVER_IP           0X043a
#define RCS_ADMIN_MODIFY_MAIL_SERVER_IP         0X043b
#define RCS_SEND_TEST_EMAIL                     0X043C

/*r End*/ 

/************No of funtions beginning with 043**********************/
#define FNS_043 13
/*******************************************************************/



/*******************************GROUP 044**************************/
#define RCS_SET_ADMIN_HOURLY_MASKS              0x0440
#define RCS_GET_ADMIN_HOURLY_MASKS              0x0441
#define RCS_SET_SNMP_SEVERITY                   0x0442
#define RCS_GET_SNMP_SEVERITY                   0x0443
#define RCS_SET_MGNTCON_SEVERITY                0x0444
#define RCS_GET_MGNTCON_SEVERITY                0x0445

/************No of funtions beginning with 044**********************/
#define FNS_044 6
/*******************************************************************/




/*******************************GROUP 045**************************/
#define RCS_ADMIN_READ_MISC_CFG                 0X0450
#define RCS_ADMIN_MODIFY_MISC_CFG               0X0451
#define RCS_ADMIN_READ_AUTO_RECOVERY_CFG        0X0452
#define RCS_ADMIN_MODIFY_AUTO_RECOVERY_CFG      0X0453
#define RCS_ADMIN_READ_TERMINAL_SERVER_CFG      0X0454
#define RCS_ADMIN_MODIFY_TERMINAL_SERVER_CFG    0X0455
/************No of funtions beginning with 045**********************/
#define FNS_045 6
/*******************************************************************/



/*******************************GROUP 046**************************/
#define RCS_ADMIN_RESET_CARD                    0X0460
#define RCS_ADMIN_SHUTDOWN_CARD                 0X0461
#define RCS_ADMIN_RESET_BATT_CHG_CNT            0X0462
#define RCS_ADMIN_SET_DATE_TIME                 0X0463
#define RCS_ADMIN_GET_DATE_TIME                 0X0464
#define RCS_ADMIN_RESTORE_FACTORY_DEFAULTS      0X0465
/************No of funtions beginning with 046**********************/
#define FNS_046 6
/*******************************************************************/



/*******************************GROUP 047**************************/
#define RCS_ADMIN_UPDATE_FIRMWARE               0X0470
/************No of funtions beginning with 047**********************/
#define FNS_047 1
/*******************************************************************/


/*******************************GROUP 048**************************/
#define RCS_ADMIN_READ_SNMPSERVER_CONFIG       0x0480
#define RCS_ADMIN_MODIFY_SNMPSERVER_CONFIG     0x0481
/************No of funtions beginning with 048**********************/
#define FNS_048 2
/*******************************************************************/



/*******************************GROUP 049**************************/

/************No of funtions beginning with 049**********************/
#define FNS_049 0
/*******************************************************************/


/*******************************GROUP 04A**************************/

/************No of funtions beginning with 04A**********************/
#define FNS_04A 0
/*******************************************************************/



/*******************************GROUP 04B**************************/

/************No of funtions beginning with 04B**********************/
#define FNS_04B 0
/*******************************************************************/


/*******************************GROUP 04C**************************/

/************No of funtions beginning with 04C**********************/
#define FNS_04C 0
/*******************************************************************/




/*******************************GROUP 04D**************************/

/************No of funtions beginning with 04D**********************/
#define FNS_04D 0
/*******************************************************************/



/*******************************GROUP 04E**************************/

/************No of funtions beginning with 04E**********************/
#define FNS_04E 0
/*******************************************************************/



/*******************************GROUP 04F**************************/

/************No of funtions beginning with 04F**********************/
#define FNS_04F 0
/*******************************************************************/


/************************************************************************/
/**************  REMOTE ADMIN AND CONFIG COMMANDS END********************/
/************************************************************************/
typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	DATE_TIME   dt;
} RCS_GET_DATE_TIME_PKT;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	DATE_TIME   dt;
} RCS_SET_DATE_TIME_PKT;


typedef struct {
	  u8 Index;
	  FOURBYTES   HourlyMasks [7];  // For admin's day-of-week preference.
			   } RCS_ADMIN_HOURLY_MASKS_PKT;

//r
typedef struct {
	  u8 Index;
	  ADMIN_MAIL_ID MailId;  //For admin's Mail Id List
	  }RCS_ADMIN_MAIL_ID_PKT;

typedef struct {
	  ADMIN_MAIL_SERVER_IP ServerIp;  //For Mail Server Ip 
	  }RCS_ADMIN_MAIL_SERVER_IP_PKT;

//r end





typedef struct {
	  u8 Index;
	  TWOBYTES SeverityMask;  // For SNMP Severity Mask
			   } RCS_SNMP_SEVERITY_MASKS_PKT;

typedef struct {
			  u8 Index;
			  TWOBYTES SeverityMask;  // For Management Console Severity Mask
			  } RCS_MGNTCON_SEVERITY_MASKS_PKT;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_ADMIN_HOURLY_MASKS_PKT Data;
} RCS_FN_GET_ADMIN_HOURLY_MASKS_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_ADMIN_HOURLY_MASKS_PKT Data;
} RCS_FN_SET_ADMIN_HOURLY_MASKS_COMMAND;

//r added
typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_ADMIN_MAIL_ID_PKT Data;
}RCS_FN_ADMIN_READ_MAIL_ID_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_ADMIN_MAIL_ID_PKT Data;
}RCS_FN_ADMIN_MODIFY_MAIL_ID_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_ADMIN_MAIL_SERVER_IP_PKT Data;
}RCS_FN_ADMIN_READ_MAIL_SERVER_IP_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_ADMIN_MAIL_SERVER_IP_PKT Data;
}RCS_FN_ADMIN_MODIFY_MAIL_SERVER_IP_COMMAND;

//r end

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_SNMP_SEVERITY_MASKS_PKT Data;
} RCS_FN_GET_SNMP_SEVERITY_MASKS_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_SNMP_SEVERITY_MASKS_PKT Data;
} RCS_FN_SET_SNMP_SEVERITY_MASKS_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_MGNTCON_SEVERITY_MASKS_PKT Data;
} RCS_FN_GET_MGNTCON_SEVERITY_MASKS_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_MGNTCON_SEVERITY_MASKS_PKT Data;
} RCS_FN_SET_MGNTCON_SEVERITY_MASKS_COMMAND;


typedef struct {
   u8 AutoRecovery;
   FOURBYTES AutoRecoveryTimer;
   u8 Reserved[11];
}RCS_AUTO_RECOVERY_CFG;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_AUTO_RECOVERY_CFG Data;
} RCS_FN_READ_AUTO_RECOVERY_CFG_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_AUTO_RECOVERY_CFG Data;
} RCS_FN_MODIFY_AUTO_RECOVERY_CFG_COMMAND;

/*
   Refer to TCP_TIME.H for detailed descriptions on how the following TCP
   time parameters are used.
*/
#define  MAX_ETHERNET_MTU  1500
#define  MIN_ETHERNET_MTU  46

#define  TCP_TTL_DEFAULT   60       /* default time to live for TCP segs */
#define  MIN_TCP_TTL       60       /* minimum time to live for TCP segs */
#define  MAX_TCP_TTL       512  /* maximum time to live for TCP segs */

#define  MIN_TCP_SRTTBASE      0
#define  MAX_TCP_SRTTBASE      4096
#define  MIN_TCP_SRTTDFLT      6
#define  MAX_TCP_SRTTDFLT      512
#define  MIN_TCP_REXMT_MIN     2
#define  MAX_TCP_REXMT_MIN     512
#define  MIN_TCP_REXMT_MAX     128
#define  MAX_TCP_REXMT_MAX     4096
/*
 * Time constants. PR_SLOWHZ equals 2 for our environment
 */
#define  TCP_MAX_SEG_LIFE  60    /* ( 30*PR_SLOWHZ) max seg lifetime */
#define  TCP_SRTTBASE      0     /* base roundtrip time; if 0, no idea yet */
#define  TCP_SRTTDFLT      6     /* (  3*PR_SLOWHZ) assumed RTT if no info */
#define  TCP_PERSMIN       10    /* (  5*PR_SLOWHZ) retransmit persistance */
#define  TCP_PERSMAX       120   /* ( 60*PR_SLOWHZ) maximum persist interval */
#define  TCP_KEEP_INIT     150   /* ( 75*PR_SLOWHZ) initial connect keep alive */
#define  TCP_KEEP_IDLE     14400 /* (120*60*PR_SLOWHZ) dflt time before probing */
#define  TCP_KEEPINTVL     150   /* ( 75*PR_SLOWHZ) default probe interval */
#define  TCP_KEEPCNT       8     /* max probes before drop */
#define  TCP_REXMT_MIN     2     /* (  1*PR_SLOWHZ) minimum allowable value */
#define  TCP_REXMT_MAX     128   /* ( 64*PR_SLOWHZ) max allowable REXMT value */
#define  TCP_LINGER_TIME   120   /* linger at most 2 minutes */
#define  TCP_MAXRXT_SHIFT  12    /* maximum retransmits */

typedef struct {
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
}RCS_TCPIP_CFG;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_TCPIP_CFG Data;
} RCS_FN_READ_TCPIP_CFG_COMMAND;

typedef struct {
   RCS_COMMAND_PACKET  RCSCmdPkt;
   RCS_TCPIP_CFG Data;
} RCS_FN_MODIFY_TCPIP_CFG_COMMAND;


/*  DHCPConfig related structures   */
typedef struct {
	u8    DHCPConfig;
	u8    Reserved [15];
}   RCS_DHCP_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_DHCP_CFG Data;
}   RCS_FN_READ_DHCP_CFG_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_DHCP_CFG Data;
}   RCS_FN_MODIFY_DHCP_CFG_COMMAND;



/*  TerminalServerConfig related structures   */
typedef struct {
	u8    TerminalServerConfig;
	u8    Reserved [15];
}   RCS_TERMINAL_SERVER_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_TERMINAL_SERVER_CFG Data;
}   RCS_FN_READ_TERMINAL_SERVER_CFG_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_TERMINAL_SERVER_CFG Data;
}   RCS_FN_MODIFY_TERMINAL_SERVER_CFG_COMMAND;

typedef struct{
	unsigned char CommunityName[32];
	unsigned int  EnterpriseID;
	unsigned char Reserved[64];
}
RCS_SNMPSERVER_CONFIG_PKT;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_SNMPSERVER_CONFIG_PKT Data;
}   RCS_FN_READ_SNMPSERVER_CFG_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_SNMPSERVER_CONFIG_PKT Data;
}   RCS_FN_MODIFY_SNMPSERVER_CFG_COMMAND;

/*  Administrator related commands  */
typedef struct {
	u8    Index;          // 0 based index of this entry in the admin list
	u8    Reserved1;      // Reserved
	char    Alias [LENGTH_OF_ALIAS];      // Administrator Alias
	char    Password [LENGTH_OF_PASSWORD];   // Administrator Password
	char    CallBackNum [LENGTH_OF_CALLBACK_NO];   // Session callback phone number
	char    PagerNum [LENGTH_OF_PAGER_NO];  // Paging service number
	char    ServiceProviderNumber[LENGTH_OF_SERVICE_NO]; //In case of Alphanumeric paging
	u8    PagerType;      // 00-none, 01-numeric, 02-alphanumeric (TAP)
	u8    Reserved2;      // Reserved
	TWOBYTES    Preferences;    // Page Bit Mask
	char    CustomCode[LENGTH_OF_CUSTOM_CODE];
	//for a customisable number to be sent on a numeric page
}   RCS_ADMIN_ENTRY;

typedef struct {
	u8    Index;
}   RCS_DELETE_ADMIN_INDEX;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_ADMIN_ENTRY     Data;
}   RCS_FN_ADD_ADMIN_ENTRY_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_ADMIN_ENTRY     Data;
}   RCS_FN_READ_ADMIN_ENTRY_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_DELETE_ADMIN_INDEX  Data;
}   RCS_FN_DELETE_ADMIN_ENTRY_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_ADMIN_ENTRY     Data;
}   RCS_FN_MODIFY_ADMIN_ENTRY_COMMAND;

/*  Net Config Commands */
typedef struct {
	FOURBYTES   IP;
	FOURBYTES   SubnetMask;
	FOURBYTES   Gateway;
}   RCS_NET_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_NET_CFG         Data;
}   RCS_FN_READ_NETWORK_CFG_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_NET_CFG         Data;
}   RCS_FN_MODIFY_NETWORK_CFG_COMMAND;

/*  ModemCfg Commands   */
typedef struct
{
	u8        ModemSelect;
	FOURBYTES   BaudRate;
	u8        DialMode;
	u8        ExtraModemInitString[LENGTH_OF_EXTRA_MODEM_STRING];
	FOURBYTES   PowerOnDelay;
	FOURBYTES   SignalDelay;
	FOURBYTES   RingDelay;
	FOURBYTES   CDDelay;
	FOURBYTES   ResponseDelay;
	FOURBYTES   HangupDelay;
	FOURBYTES   ConnectTimeOut;
	FOURBYTES   DetectTimeOut;
}   RCS_MODEM_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_MODEM_CFG       Data;
}   RCS_FN_READ_MODEM_CFG_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_MODEM_CFG       Data;
}   RCS_FN_MODIFY_MODEM_CFG_COMMAND;

/*  SNMP Destination Commands   */
typedef struct
{
	u8        index;
	FOURBYTES   IP;
	u8        ComunityName[LENGTH_OF_COMUNITY_NAME];
}   RCS_SNMP_DEST_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_SNMP_DEST_CFG   Data;
}   RCS_FN_ADD_SNMP_DESTINATION_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_SNMP_DEST_CFG   Data;
}   RCS_FN_READ_SNMP_DESTINATION_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_SNMP_DEST_CFG   Data;
}   RCS_FN_MODIFY_SNMP_DESTINATION_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_SNMP_DEST_CFG   Data;
}   RCS_FN_DELETE_SNMP_DESTINATION_COMMAND;

/*  Management Console Commands */
typedef struct
{
	u8    index;
	u8    PhoneNumber[LENGTH_OF_PHONENUMBER];
}   RCS_MGNT_CON_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_MGNT_CON_CFG    Data;
}   RCS_FN_ADD_MANAGEMENT_CONSOLE_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_MGNT_CON_CFG    Data;
}   RCS_FN_READ_MANAGEMENT_CONSOLE_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_MGNT_CON_CFG    Data;
}   RCS_FN_MODIFY_MANAGEMENT_CONSOLE_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_MGNT_CON_CFG    Data;
}   RCS_FN_DELETE_MANAGEMENT_CONSOLE_COMMAND;

/*  Alert Table Commands    */
typedef struct
{
	u8    AlertMethod [3];
	u8    ReservedForExtraMethods [5];
	u8    RetriesMethod [3];
	u8    ReservedForExtraMethodRetries [5];
	TWOBYTES    NotificationFrequency;
}   RCS_ALERT_ORDER_TABLE_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_ALERT_ORDER_TABLE_CFG   Data;
}   RCS_FN_READ_ALERT_TABLE_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_ALERT_ORDER_TABLE_CFG   Data;
}   RCS_FN_MODIFY_ALERT_TABLE_COMMAND;

/*  Force Save Config Command   */
typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
}   RCS_FN_FORCE_SAVE_CONFIG_COMMAND;

/*  Admin Test Page Command     */
typedef struct {
	u8    Index;
}   RCS_TEST_PAGE_INDEX;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_TEST_PAGE_INDEX Data;
}   RCS_ADMIN_TEST_PAGE_COMMAND;

/*  PPP Config Commands     */
typedef struct
{
	FOURBYTES   IPPoolStart;
}   RCS_PPP_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_PPP_CFG         Data;
}   RCS_FN_READ_PPP_CFG_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_PPP_CFG         Data;
}   RCS_FN_MODIFY_PPP_CFG_COMMAND;

/*  Dialout Config Commands     */
typedef struct
{
	char    Alias [LENGTH_OF_DIALOUT_ALIAS];       // Dial out Alias
	char    Password [LENGTH_OF_DIALOUT_PASSWORD]; // Dial out Password
}   RCS_DIALOUT_CFG;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_DIALOUT_CFG     Data;
}   RCS_FN_READ_DIALOUT_CFG_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_DIALOUT_CFG     Data;
}   RCS_FN_MODIFY_DIALOUT_CFG_COMMAND;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
}   RCS_FN_RESTORE_FACTORY_DEFAULTS_COMMAND;

//raja
/*  Admin Test Email Command     */
typedef struct {
	u8    Index;
	u8    Reserved[15];
}   RCS_TEST_EMAIL_ARGS;

typedef struct {
	RCS_COMMAND_PACKET  RCSCmdPkt;
	RCS_TEST_EMAIL_ARGS Data;
}   RCS_FN_TEST_EMAIL_COMMAND;


#endif

