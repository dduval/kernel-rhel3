/*******************************************************************

	File Name   :   ADMIN.H

	Author      :   K.V.Subash & Parthiban Baskar

	Modification :   J.Raja 

	Purpose for  :   Alert by Mail is included
	Modification     8 Feb 2000


	Date        :   29 January 1998

	Purpose     :   Equates, data types and structures for the
					administrator list

	Copyright   :   American Megatrends Inc., 1997-1998

********************************************************************/

#ifndef  __ADMIN_H__
#define  __ADMIN_H__

#include "dtypes.h"

#define MAX_ADMINS  16
#define LENGTH_OF_ALIAS 32
#define LENGTH_OF_PASSWORD 32
#define LENGTH_OF_CALLBACK_NO 32
#define LENGTH_OF_PAGER_NO    32
#define LENGTH_OF_CUSTOM_CODE 50
//mjb$$
#define LENGTH_OF_SERVICE_NO 32
//mjb$$

typedef struct Admin_Entry_Type_Tag
{
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
} AdminEntry;

//raja 
typedef struct AdminMailIdList
{
char MailId[64];
u8 Reserved[16];
}ADMIN_MAIL_ID;

typedef struct AdminMailServer
{
u8 TypeOfAddress;
char ServerName[64];
FOURBYTES IpAddress;
}ADMIN_MAIL_SERVER_IP;

//raja end
#endif

