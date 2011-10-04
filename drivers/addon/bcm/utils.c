
/*
 *  Broadcom Cryptonet Driver software is distributed as is, without any warranty
 *  of any kind, either express or implied as further specified in the GNU Public
 *  License. This software may be used and distributed according to the terms of
 *  the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */
/******************************************************************************
 *
 *  Copyright 2000
 *  Broadcom Corporation
 *  16215 Alton Parkway
 *  PO Box 57013
 *  Irvine CA 92619-7013
 *
 *****************************************************************************/
/* 
 * Broadcom Corporation uBSec SDK 
 */
/*
 * utils.c: Driver utility functions.
 */
/*
 * Revision History:
 *
 * May 2000 SOR/JTT Created.
 * March 2001 PW Release for Linux 2.4 UP and SMP kenel
 */

#include "cdevincl.h"	
#include <linux/sched.h>


#ifdef DVT
  #define MAX_SLEEP_TIME 100*HZ
#else
  #define MAX_SLEEP_TIME 3*HZ
#endif /* DVT */


void tv_sub(struct timeval *out, struct timeval *in)
{
  if (in->tv_usec > out->tv_usec)  {
	out->tv_sec--;
	out->tv_usec += 1000000;
  }
  out->tv_sec -= in->tv_sec;
  out->tv_usec -= in->tv_usec;
}

void start_time(struct timeval *tv_start)  
{ 
  do_gettimeofday(tv_start);
}


unsigned long stop_time(struct timeval *tv_start)
{
  struct timeval tv_stop;

  do_gettimeofday(&tv_stop);
  tv_sub(&tv_stop, tv_start);
  return ( tv_stop.tv_sec * 1000000 + tv_stop.tv_usec );
}

#ifndef LINUX2dot2

int Gotosleep(wait_queue_head_t *WaitQ)
{
   init_waitqueue_head(WaitQ);
   sleep_on_timeout(WaitQ, MAX_SLEEP_TIME);
   return 0;
}

void wakeup(wait_queue_head_t *WaitQ)
{
   wake_up(WaitQ);
}

#else
int Gotosleep(struct wait_queue **WaitQ)
{
 return sleep_on_timeout(WaitQ,MAX_SLEEP_TIME);
 
#ifdef LATEST_VERSION
  module_interruptible_sleep_on_timeout(WaitQ,MAX_SLEEP_TIME);
#else
  interruptible_sleep_on_timeout(WaitQ,MAX_SLEEP_TIME);
#endif
}

void wakeup(struct wait_queue **WaitQ)
{
  module_wake_up(WaitQ);
}                 
#endif


void CmdCompleteCallback(unsigned long CallBackContext,ubsec_Status_t Result)
{
	
  CommandContext_pt  pCommandContext=(CommandContext_pt)CallBackContext;
  struct timeval	tv_stop;

  memset(&tv_stop, 0, sizeof(tv_stop));
  do_gettimeofday(&tv_stop);
  tv_sub(&tv_stop, &pCommandContext->tv_start);
  pCommandContext->tv_start = tv_stop;

  (pCommandContext->CallBackStatus)++;
  pCommandContext->Status=Result;

#ifdef GOTOSLEEP
       wakeup(&pCommandContext->WaitQ);
#endif
  return;
}



int power_of_2(unsigned long number)
{
  if (number < 2) 
    return 0; 
  while (!(number & 0x01)) 
    number = number >> 1;
  return (number == 1);
}



unsigned long next_smaller_power_of_2(unsigned long number)
{
  int shift = 0;
  if (!number) 
    return 0; 
  while (!(number & 0x80000000)) {
    number = number << 1;
    shift++;
  }
  return (0x80000000 >> shift);
}

