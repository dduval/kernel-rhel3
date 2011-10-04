/*
 ****************************************************************************
 *
 *   File Name   :  POST.H
 *
 *   Author      :  Baskar Parthiban
 *
 *   Date        :  20 Apr 1998
 *
 *   Purpose     :  This file has #defines to help capture, saving and
 *                  retrieval of the POST codes of the host during
 *                  startup.
 *
 *   Copyright   :  American Megatrends Inc., (C) 1997-1998
 *                  All rights reserved.
 *
 *
 ****************************************************************************
 */
#ifndef __AMI_POST_H__
#define __AMI_POST_H__

#include    "rcs.h"

#define POSTCodeSnoopPort   (BYTE *)0xC000181E
#define MAX_POST_LOGS 4
#define MAX_POST_CODES  512

extern  TWOBYTES    NumOfPOSTCodes;

typedef struct _POSTLogTag
{
    BYTE        POSTCodes [MAX_POST_CODES];
} POSTLogStruct;

typedef struct _POSTLogPktToRemoteTAG
{
    RCS_COMMAND_PACKET RCSCmdPkt;
//    BYTE        POSTCodes [MAX_POST_CODES];
    POSTLogStruct   strPOSTLog;

} POST_LOG_PKT_TO_REMOTE;

typedef struct _POSTLogTypeTAG{
   TWOBYTES NumOfCodes;
   BYTE     Log[MAX_POST_CODES];
   void  *  pprev;
   void  *  pnext;
}POST_LOG_TYPE;

#endif

 /*---------------------------- End of POST.H -------------------------------*/
