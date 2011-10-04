/*************************************************************************
*	File	:	BATRCS.C
*
*	Author	:	Prabhu.C
*
*	Purpose	:   Definition file for Battery Monitor module.
*
*	Date	:	23rd Feb 1998
*
*************************************************************************/
#ifndef __BATMON_H__
#define __BATMON_H__

#include "dtypes.h"

#define MAX_COUNTDOWN_TIMER   1800  // Battery charge life in seconds
#define BATTERY_CHECK_PERIOD  30    // Frequency of battery checks in seconds

#define BAT_GOOD		0x00
#define BAT_ABSENT		0x01
#define BAT_LOW			0x02
#define BAT_BAD			0x03
#define BAT_CHECKING 	0x04

#define BAT_NOT_CHARGING		0x00
#define BAT_FAST_CHARGING		0x01
#define BAT_TRICKLE_CHARGING    0x02

#define ALERT_LIMIT	900
#define MAX_LIMIT	1000

//Raid uses alert limit as 1000 and maximum limit as 1100

#define BATTERY_PACK_PORT	(u8 *)0xfd500000
#define BAT_CHARGE_CON_PORT	(u8 *)0xfd500002
#define BAT_BACKUP_CON_PORT	(u8 *)0xfd500004

/* BIT definitions */
#define	STLED2	0x80	// Gives charge action status
#define	STLED1	0x40	// Gives charge action status
#define BV145	0x20	// Indicates whether the voltage of the battery pack
						// is above or below 1.45v
#define	NORMAL	0x10	// Battery pack is low (< or > 0.9v per cell)
#define	CHGEN	0x08	// Enable Fast Charging
#define	INH		0x04	// Charge Inhibit
#define	BE		0x02	// Battery backup enable
#define	BC		0x01	// Battery backup connect

/*************************************************************************/

typedef struct GET_BAT_STATUS_TYPE_tag
{
	u8 BatteryStatus;
	u8 ChargeState;
	u16 FastChargeCount;
}GET_BAT_STATUS_TYPE;

#endif

