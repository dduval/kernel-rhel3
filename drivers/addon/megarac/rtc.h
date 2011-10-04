/*
 ****************************************************************************
 *
 *          File Name   :       RTC.H
 *
 *          Author      :       Vinesh C.S.
 *
 *          Date        :       16 Feb 1998
 *
 *          Purpose     :       This code contains Time and Date Related Code
 *
 *          Copyright   :       American Megatrends Inc., (C) 1997-1998
 *                              All rights reserved.
 *
 *
 ****************************************************************************
 */
#ifndef __AMI_RTC_H__
#define __AMI_RTC_H__

typedef struct {
    unsigned short Year;
    unsigned char  Mon;
    unsigned char  Date;
    unsigned char  Hour;
    unsigned char  Min;
    unsigned char  Sec;
    unsigned char  Res;
} DATE_TIME;

void SetDateTime(DATE_TIME *dt);
unsigned char GetDateTime(DATE_TIME *dt);

void DriverDelay(unsigned long msec);

#endif

 /*---------------------------- End of RTC.H -------------------------------*/
