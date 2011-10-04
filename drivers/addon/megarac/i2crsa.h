/*
 ****************************************************************************
 *
 *          File Name   :       I2CRSA.H
 *
 *          Author      :       Chambers, N.T.
 *
 *          Date        :       5 May 1999
 *
 *          Purpose     :       I2CRSA related defines
 *
 *          Copyright   :       American Megatrends Inc., (C) 1997-1999
 *                              All rights reserved.
 *
 ****************************************************************************
*/

#ifndef __I2CRSA_H__
#define __I2CRSA_H__

//Mem mapped addresses
#define I2CDATALOC   (char *)0xFDA00000
#define I2CCMDLOC    (char *)0xFDA00001

#define I2C_REPEATED_START_ACCESS  0x602   /* I2C Repeated Start Access Ioctl */


//Take care that the RAWPBuf[I2C_BUFFER_LENGTH] in Sdk.c is not exceeded
#define I2C_MAX_DATA_BYTES    10
#define I2C_MAX_TRANSACTIONS  10


typedef struct I2C_REPEATED_START_TYPE_tag
{
   unsigned char TargetAddr;
   unsigned char NumberOfBytes;
   unsigned char Data[I2C_MAX_DATA_BYTES];
}I2C_REPEATED_START_TYPE;

typedef struct I2C_REPEATED_START_WITH_STATUS_tag
{
   unsigned char NumberOfTransactions;
   unsigned char Status;
   I2C_REPEATED_START_TYPE Transaction[I2C_MAX_TRANSACTIONS];
}I2C_REPEATED_START_WITH_STATUS_TYPE;

#endif
