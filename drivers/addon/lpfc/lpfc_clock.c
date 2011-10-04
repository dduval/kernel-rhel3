/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_clock.c 1.23.1.2 2004/05/25 14:48:50EDT jselx Exp  $
 */

#include <linux/version.h>
#include <linux/spinlock.h>


#include <linux/blk.h>
#include <scsi.h>

#include "lpfc_hw.h"
#include "lpfc_mem.h"
#include "lpfc_sli.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_scsi.h"
#include "lpfc_crtn.h"

void
lpfc_start_timer(lpfcHBA_t * phba, 
	unsigned long tmo, struct timer_list *ptimer,
	void (*func) (unsigned long), unsigned long data1, unsigned long data2) 
{
	struct clk_data *clkData;

	clkData = kmalloc(sizeof(struct clk_data), GFP_ATOMIC);
	clkData->timeObj = ptimer; 
	clkData->phba = phba;
	clkData->clData1 = data1;
	clkData->clData2 = data2;

	init_timer(ptimer);
	ptimer->function = func;
	ptimer->expires = jiffies + HZ * tmo;
	ptimer->data = (unsigned long)clkData;
	list_add((struct list_head *)clkData, &phba->timerList);
	add_timer(ptimer);
}

void
lpfc_stop_timer(struct clk_data *clkData)
{
	struct timer_list *ptimer;

	ptimer = clkData->timeObj;
	del_timer(ptimer);
	ptimer->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);
}
