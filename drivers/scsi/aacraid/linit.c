/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *   linit.c
 *
 * Abstract: Linux Driver entry module for Adaptec RAID Array Controller
 */

#define AAC_DRIVER_VERSION		"1.1-5"
#ifndef AAC_DRIVER_BRANCH
#define AAC_DRIVER_BRANCH		""
#endif
#define AAC_DRIVER_BUILD_DATE		__DATE__ " " __TIME__
#define AAC_DRIVERNAME			"aacraid"

#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>

#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#include "scsi_dump.h"
#include <linux/blk.h>	/* for io_request_lock definition */
#if (1) ? defined(__x86_64__) : defined(CONFIG_COMPAT)
# include <asm-x86_64/ioctl32.h>
  /* Cast the function, since sys_ioctl does not match */
# define aac_ioctl32(x,y) register_ioctl32_conversion((x), \
    (int(*)(unsigned int,unsigned int,unsigned long,struct file*))(y))
#endif
# include <asm/uaccess.h>
#include <linux/reboot.h>

#include "aacraid.h"

#ifdef AAC_DRIVER_BUILD
#define _str(x) #x
#define str(x) _str(x)
#define AAC_DRIVER_FULL_VERSION	AAC_DRIVER_VERSION "[" str(AAC_DRIVER_BUILD) "]" AAC_DRIVER_BRANCH
#else
#define AAC_DRIVER_FULL_VERSION	AAC_DRIVER_VERSION AAC_DRIVER_BRANCH " " AAC_DRIVER_BUILD_DATE
#endif

MODULE_AUTHOR("Red Hat Inc and Adaptec");
MODULE_DESCRIPTION("Dell PERC2, 2/Si, 3/Si, 3/Di, "
		   "Adaptec Advanced Raid Products, "
		   "HP NetRAID-4M, IBM ServeRAID & ICP SCSI driver");
MODULE_LICENSE("GPL");
#ifdef MODULE_VERSION
MODULE_VERSION(AAC_DRIVER_FULL_VERSION);
#endif

LIST_HEAD(aac_devices);
static int aac_cfg_major = -1;
char aac_driver_version[] = AAC_DRIVER_FULL_VERSION;

/*
 * Because of the way Linux names scsi devices, the order in this table has
 * become important.  Check for on-board Raid first, add-in cards second.
 *
 * Note: The last field is used to index into aac_drivers below.
 */
static struct pci_device_id aac_pci_tbl[] = {
	{ 0x1028, 0x0001, 0x1028, 0x0001, 0, 0, 0 }, /* PERC 2/Si (Iguana/PERC2Si) */
	{ 0x1028, 0x0002, 0x1028, 0x0002, 0, 0, 1 }, /* PERC 3/Di (Opal/PERC3Di) */
	{ 0x1028, 0x0003, 0x1028, 0x0003, 0, 0, 2 }, /* PERC 3/Si (SlimFast/PERC3Si */
	{ 0x1028, 0x0004, 0x1028, 0x00d0, 0, 0, 3 }, /* PERC 3/Di (Iguana FlipChip/PERC3DiF */
	{ 0x1028, 0x0002, 0x1028, 0x00d1, 0, 0, 4 }, /* PERC 3/Di (Viper/PERC3DiV) */
	{ 0x1028, 0x0002, 0x1028, 0x00d9, 0, 0, 5 }, /* PERC 3/Di (Lexus/PERC3DiL) */
	{ 0x1028, 0x000a, 0x1028, 0x0106, 0, 0, 6 }, /* PERC 3/Di (Jaguar/PERC3DiJ) */
	{ 0x1028, 0x000a, 0x1028, 0x011b, 0, 0, 7 }, /* PERC 3/Di (Dagger/PERC3DiD) */
	{ 0x1028, 0x000a, 0x1028, 0x0121, 0, 0, 8 }, /* PERC 3/Di (Boxster/PERC3DiB) */
	{ 0x9005, 0x0283, 0x9005, 0x0283, 0, 0, 9 }, /* catapult */
	{ 0x9005, 0x0284, 0x9005, 0x0284, 0, 0, 10 }, /* tomcat */
	{ 0x9005, 0x0285, 0x9005, 0x0286, 0, 0, 11 }, /* Adaptec 2120S (Crusader) */
	{ 0x9005, 0x0285, 0x9005, 0x0285, 0, 0, 12 }, /* Adaptec 2200S (Vulcan) */
	{ 0x9005, 0x0285, 0x9005, 0x0287, 0, 0, 13 }, /* Adaptec 2200S (Vulcan-2m) */
	{ 0x9005, 0x0285, 0x17aa, 0x0286, 0, 0, 14 }, /* Legend S220 (Legend Crusader) */
	{ 0x9005, 0x0285, 0x17aa, 0x0287, 0, 0, 15 }, /* Legend S230 (Legend Vulcan) */

	{ 0x9005, 0x0285, 0x9005, 0x0288, 0, 0, 16 }, /* Adaptec 3230S (Harrier) */
	{ 0x9005, 0x0285, 0x9005, 0x0289, 0, 0, 17 }, /* Adaptec 3240S (Tornado) */
	{ 0x9005, 0x0285, 0x9005, 0x028a, 0, 0, 18 }, /* ASR-2020ZCR SCSI PCI-X ZCR (Skyhawk) */
	{ 0x9005, 0x0285, 0x9005, 0x028b, 0, 0, 19 }, /* ASR-2025ZCR SCSI SO-DIMM PCI-X ZCR (Terminator) */
	{ 0x9005, 0x0286, 0x9005, 0x028c, 0, 0, 20 }, /* ASR-2230S + ASR-2230SLP PCI-X (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x028d, 0, 0, 21 }, /* ASR-2130S (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x029b, 0, 0, 22 }, /* AAR-2820SA (Intruder) */
	{ 0x9005, 0x0286, 0x9005, 0x029c, 0, 0, 23 }, /* AAR-2620SA (Intruder) */
	{ 0x9005, 0x0286, 0x9005, 0x029d, 0, 0, 24 }, /* AAR-2420SA (Intruder) */
	{ 0x9005, 0x0286, 0x9005, 0x029e, 0, 0, 25 }, /* ICP9024R0 (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x029f, 0, 0, 26 }, /* ICP9014R0 (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x02a0, 0, 0, 27 }, /* ICP9047MA (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x02a1, 0, 0, 28 }, /* ICP9087MA (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x02a3, 0, 0, 29 }, /* ICP5085AU (Hurricane) */
	{ 0x9005, 0x0285, 0x9005, 0x02a4, 0, 0, 30 }, /* ICP9085LI (Marauder-X) */
	{ 0x9005, 0x0285, 0x9005, 0x02a5, 0, 0, 31 }, /* ICP5085BR (Marauder-E) */
	{ 0x9005, 0x0286, 0x9005, 0x02a6, 0, 0, 32 }, /* ICP9067MA (Intruder-6) */
	{ 0x9005, 0x0287, 0x9005, 0x0800, 0, 0, 33 }, /* Themisto Jupiter Platform */
	{ 0x9005, 0x0200, 0x9005, 0x0200, 0, 0, 33 }, /* Themisto Jupiter Platform */
	{ 0x9005, 0x0286, 0x9005, 0x0800, 0, 0, 34 }, /* Callisto Jupiter Platform */
	{ 0x9005, 0x0285, 0x9005, 0x028e, 0, 0, 35 }, /* ASR-2020SA SATA PCI-X ZCR (Skyhawk) */
	{ 0x9005, 0x0285, 0x9005, 0x028f, 0, 0, 36 }, /* ASR-2025SA SATA SO-DIMM PCI-X ZCR (Terminator) */
	{ 0x9005, 0x0285, 0x9005, 0x0290, 0, 0, 37 }, /* AAR-2410SA PCI SATA 4ch (Jaguar II) */
	{ 0x9005, 0x0285, 0x1028, 0x0291, 0, 0, 38 }, /* CERC SATA RAID 2 PCI SATA 6ch (DellCorsair) */
	{ 0x9005, 0x0285, 0x9005, 0x0292, 0, 0, 39 }, /* AAR-2810SA PCI SATA 8ch (Corsair-8) */
	{ 0x9005, 0x0285, 0x9005, 0x0293, 0, 0, 40 }, /* AAR-21610SA PCI SATA 16ch (Corsair-16) */
	{ 0x9005, 0x0285, 0x9005, 0x0294, 0, 0, 41 }, /* ESD SO-DIMM PCI-X SATA ZCR (Prowler) */
	{ 0x9005, 0x0285, 0x103C, 0x3227, 0, 0, 42 }, /* AAR-2610SA PCI SATA 6ch */
	{ 0x9005, 0x0285, 0x9005, 0x0296, 0, 0, 43 }, /* ASR-2240S (SabreExpress) */
	{ 0x9005, 0x0285, 0x9005, 0x0297, 0, 0, 44 }, /* ASR-4005SAS */
	{ 0x9005, 0x0285, 0x1014, 0x02F2, 0, 0, 45 }, /* IBM 8i (AvonPark) */
	{ 0x9005, 0x0285, 0x1014, 0x0312, 0, 0, 45 }, /* IBM 8i (AvonPark Lite) */
	{ 0x9005, 0x0286, 0x1014, 0x9580, 0, 0, 46 }, /* IBM 8k/8k-l8 (Aurora) */
	{ 0x9005, 0x0286, 0x1014, 0x9540, 0, 0, 47 }, /* IBM 8k/8k-l4 (Aurora Lite) */
	{ 0x9005, 0x0285, 0x9005, 0x0298, 0, 0, 48 }, /* ASR-4000SAS (BlackBird) */
	{ 0x9005, 0x0285, 0x9005, 0x0299, 0, 0, 49 }, /* ASR-4800SAS (Marauder-X) */
	{ 0x9005, 0x0285, 0x9005, 0x029a, 0, 0, 50 }, /* ASR-4805SAS (Marauder-E) */
	{ 0x9005, 0x0286, 0x9005, 0x02a2, 0, 0, 51 }, /* ASR-4810SAS (Hurricane */

	{ 0x9005, 0x0285, 0x1028, 0x0287, 0, 0, 52 }, /* Perc 320/DC*/
	{ 0x1011, 0x0046, 0x9005, 0x0365, 0, 0, 53 }, /* Adaptec 5400S (Mustang)*/
	{ 0x1011, 0x0046, 0x9005, 0x0364, 0, 0, 54 }, /* Adaptec 5400S (Mustang)*/
	{ 0x1011, 0x0046, 0x9005, 0x1364, 0, 0, 55 }, /* Dell PERC2/QC */
	{ 0x1011, 0x0046, 0x103c, 0x10c2, 0, 0, 56 }, /* HP NetRAID-4M */

	{ 0x9005, 0x0285, 0x1028, PCI_ANY_ID, 0, 0, 57 }, /* Dell Catchall */
	{ 0x9005, 0x0285, 0x17aa, PCI_ANY_ID, 0, 0, 58 }, /* Legend Catchall */
	{ 0x9005, 0x0285, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 59 }, /* Adaptec Catch All */
	{ 0x9005, 0x0286, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 60 }, /* Adaptec Rocket Catch All */
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, aac_pci_tbl);

/*
 * dmb - For now we add the number of channels to this structure.  
 * In the future we should add a fib that reports the number of channels
 * for the card.  At that time we can remove the channels from here
 */
static struct aac_driver_ident aac_drivers[] = {
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 2/Si (Iguana/PERC2Si) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 3/Di (Opal/PERC3Di) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 3/Si (SlimFast/PERC3Si */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 3/Di (Iguana FlipChip/PERC3DiF */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 3/Di (Viper/PERC3DiV) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 3/Di (Lexus/PERC3DiL) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 1, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 3/Di (Jaguar/PERC3DiJ) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 3/Di (Dagger/PERC3DiD) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* PERC 3/Di (Boxster/PERC3DiB) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "catapult        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* catapult */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "tomcat          ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* tomcat */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2120S   ", 1, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Adaptec 2120S (Crusader) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2200S   ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Adaptec 2200S (Vulcan) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2200S   ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Adaptec 2200S (Vulcan-2m) */
	{ aac_rx_init, "aacraid",  "Legend  ", "Legend S220     ", 1, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Legend S220 (Legend Crusader) */
	{ aac_rx_init, "aacraid",  "Legend  ", "Legend S230     ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Legend S230 (Legend Vulcan) */

	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 3230S   ", 2 }, /* Adaptec 3230S (Harrier) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 3240S   ", 2 }, /* Adaptec 3240S (Tornado) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2020ZCR     ", 2 }, /* ASR-2020ZCR SCSI PCI-X ZCR (Skyhawk) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2025ZCR     ", 2 }, /* ASR-2025ZCR SCSI SO-DIMM PCI-X ZCR (Terminator) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "ASR-2230S PCI-X ", 2 }, /* ASR-2230S + ASR-2230SLP PCI-X (Lancer) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "ASR-2130S PCI-X ", 1 }, /* ASR-2130S (Lancer) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "AAR-2820SA      ", 1 }, /* AAR-2820SA (Intruder) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "AAR-2620SA      ", 1 }, /* AAR-2620SA (Intruder) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "AAR-2420SA      ", 1 }, /* AAR-2420SA (Intruder) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9024R0       ", 2 }, /* ICP9024R0 (Lancer) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9014R0       ", 1 }, /* ICP9014R0 (Lancer) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9047MA       ", 1 }, /* ICP9047MA (Lancer) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9087MA       ", 1 }, /* ICP9087MA (Lancer) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP5085AU       ", 1 }, /* ICP5085AU (Hurricane) */
	{ aac_rx_init, "aacraid",  "ICP     ", "ICP9085LI       ", 1 }, /* ICP9085LI (Marauder-X) */
	{ aac_rx_init, "aacraid",  "ICP     ", "ICP5085BR       ", 1 }, /* ICP5085BR (Marauder-E) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9067MA       ", 1 }, /* ICP9067MA (Intruder-6) */
	{ NULL        , "aacraid",  "ADAPTEC ", "Themisto        ", 0, AAC_QUIRK_SLAVE }, /* Jupiter Platform */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "Callisto        ", 2, AAC_QUIRK_MASTER }, /* Jupiter Platform */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2020SA       ", 1 }, /* ASR-2020SA SATA PCI-X ZCR (Skyhawk) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2025SA       ", 1 }, /* ASR-2025SA SATA SO-DIMM PCI-X ZCR (Terminator) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2410SA SATA ", 1 }, /* AAR-2410SA PCI SATA 4ch (Jaguar II) */
	{ aac_rx_init, "aacraid",  "DELL    ", "CERC SR2        ", 1 }, /* CERC SATA RAID 2 PCI SATA 6ch (DellCorsair) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2810SA SATA ", 1 }, /* AAR-2810SA PCI SATA 8ch (Corsair-8) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-21610SA SATA", 1 }, /* AAR-21610SA PCI SATA 16ch (Corsair-16) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2026ZCR     ", 1 }, /* ESD SO-DIMM PCI-X SATA ZCR (Prowler) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2610SA      ", 1 }, /* SATA 6Ch (Bearcat) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2240S       ", 1 }, /* ASR-2240S (SabreExpress) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4005SAS     ", 1 }, /* ASR-4005SAS */
	{ aac_rx_init, "ServeRAID","IBM     ", "ServeRAID 8i    ", 1 }, /* IBM 8i (AvonPark) */
	{ aac_rkt_init, "ServeRAID","IBM     ", "ServeRAID 8k-l8 ", 1 }, /* IBM 8k/8k-l8 (Aurora) */
	{ aac_rkt_init, "ServeRAID","IBM     ", "ServeRAID 8k-l4 ", 1 }, /* IBM 8k/8k-l4 (Aurora Lite) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4000SAS     ", 1 }, /* ASR-4000SAS (BlackBird & AvonPark) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4800SAS     ", 1 }, /* ASR-4800SAS (Marauder-X) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4805SAS     ", 1 }, /* ASR-4805SAS (Marauder-E) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "ASR-4810SAS     ", 1 }, /* ASR-4810SAS (Hurricane) */

	{ aac_rx_init, "percraid", "DELL    ", "PERC 320/DC     ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Perc 320/DC*/
	{ aac_sa_init, "aacraid",  "ADAPTEC ", "Adaptec 5400S   ", 4, AAC_QUIRK_34SG }, /* Adaptec 5400S (Mustang)*/
	{ aac_sa_init, "aacraid",  "ADAPTEC ", "AAC-364         ", 4, AAC_QUIRK_34SG }, /* Adaptec 5400S (Mustang)*/
	{ aac_sa_init, "percraid", "DELL    ", "PERCRAID        ", 4, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Dell PERC2/QC */
	{ aac_sa_init, "hpnraid",  "HP      ", "NetRAID         ", 4, AAC_QUIRK_34SG }, /* HP NetRAID-4M */

	{ aac_rx_init, "aacraid",  "DELL    ", "RAID            ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Dell Catchall */
	{ aac_rx_init, "aacraid",  "Legend  ", "RAID            ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Legend Catchall */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "RAID            ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Adaptec Catch All */
	{ aac_rkt_init, "aacraid", "ADAPTEC ", "RAID            ", 2 } /* Adaptec Rocket Catch All */
};

#if (1) ? defined(__x86_64__) : defined(CONFIG_COMPAT)
/* 
 * Promote 32 bit apps that call get_next_adapter_fib_ioctl to 64 bit version 
 */
static int aac_get_next_adapter_fib_ioctl(unsigned int fd, unsigned int cmd, 
		unsigned long arg, struct file *file)
{
	struct fib_ioctl f;
	mm_segment_t fs;
	int retval;

	memset (&f, 0, sizeof(f));
	if (copy_from_user(&f, (void __user *)arg, sizeof(f) - sizeof(u32)))
		return -EFAULT;
	fs = get_fs();
	set_fs(get_ds());
	retval = sys_ioctl(fd, cmd, (unsigned long)&f);
	set_fs(fs);
	return retval;
}
#endif


/**
 *	aac_detect	-	Probe for aacraid cards
 *	@template: SCSI driver template
 *
 *	This is but a stub to convince the 2.4 scsi layer to scan targets,
 *	the pci scan has already picked up the adapters.
 */
static int aac_detect(Scsi_Host_Template *template)
{
	return template->present = 1;
}


/**
 *	aac_queuecommand	-	queue a SCSI command
 *	@cmd:		SCSI command to queue
 *	@done:		Function to call on command completion
 *
 *	Queues a command for execution by the associated Host Adapter.
 *
 *	TODO: unify with aac_scsi_cmd().
 */ 

static int aac_queuecommand(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	cmd->scsi_done = done;
	cmd->SCp.phase = AAC_OWNER_LOWLEVEL;
	return (aac_scsi_cmd(cmd) ? FAILED : 0);
} 

/**
 *	aac_info		-	Returns the host adapter name
 *	@shost:		Scsi host to report on
 *
 *	Returns a static string describing the device in question
 */

static const char *aac_info(struct Scsi_Host *shost)
{
	struct aac_dev *dev;
	dev = (struct aac_dev *)shost->hostdata;
	if (!dev
	 || (dev->cardtype >= (sizeof(aac_drivers)/sizeof(aac_drivers[0]))))
		return shost->hostt->name;
	if (dev->scsi_host_ptr != shost)
		return shost->hostt->name;
	return aac_drivers[dev->cardtype].name;
}

/**
 *	aac_get_driver_ident
 * 	@devtype: index into lookup table
 *
 * 	Returns a pointer to the entry in the driver lookup table.
 */

struct aac_driver_ident* aac_get_driver_ident(int devtype)
{
	return &aac_drivers[devtype];
}

/**
 *	aac_biosparm	-	return BIOS parameters for disk
 *	@disk: SCSI disk object to process
 *	@device: kdev_t of the disk in question
 *	@sdev: The scsi device corresponding to the disk
 *	@bdev: the block device corresponding to the disk
 *	@capacity: the sector capacity of the disk
 *	@geom: geometry block to fill in
 *
 *	Return the Heads/Sectors/Cylinders BIOS Disk Parameters for Disk.  
 *	The default disk geometry is 64 heads, 32 sectors, and the appropriate 
 *	number of cylinders so as not to exceed drive capacity.  In order for 
 *	disks equal to or larger than 1 GB to be addressable by the BIOS
 *	without exceeding the BIOS limitation of 1024 cylinders, Extended 
 *	Translation should be enabled.   With Extended Translation enabled, 
 *	drives between 1 GB inclusive and 2 GB exclusive are given a disk 
 *	geometry of 128 heads and 32 sectors, and drives above 2 GB inclusive 
 *	are given a disk geometry of 255 heads and 63 sectors.  However, if 
 *	the BIOS detects that the Extended Translation setting does not match 
 *	the geometry in the partition table, then the translation inferred 
 *	from the partition table will be used by the BIOS, and a warning may 
 *	be displayed.
 */
 
static int aac_biosparm(
	Scsi_Disk *disk, kdev_t dev,
	int *geom)
{
	struct diskparm *param = (struct diskparm *)geom;
	struct buffer_head * buf;
	sector_t capacity = disk->capacity;

	dprintk((KERN_DEBUG "aac_biosparm.\n"));

	/*
	 *	Assuming extended translation is enabled - #REVISIT#
	 */
	if (capacity >= 2 * 1024 * 1024) { /* 1 GB in 512 byte sectors */
		if(capacity >= 4 * 1024 * 1024) { /* 2 GB in 512 byte sectors */
			param->heads = 255;
			param->sectors = 63;
		} else {
			param->heads = 128;
			param->sectors = 32;
		}
	} else {
		param->heads = 64;
		param->sectors = 32;
	}

	param->cylinders = cap_to_cyls(capacity, param->heads * param->sectors);

	/* 
	 *	Read the first 1024 bytes from the disk device, if the boot
	 *	sector partition table is valid, search for a partition table
	 *	entry whose end_head matches one of the standard geometry 
	 *	translations ( 64/32, 128/32, 255/63 ).
	 */
	buf = bread(MKDEV(MAJOR(dev), MINOR(dev)&~0xf), 0, block_size(dev));
	if(buf == NULL)
		return 0;
	if(*(unsigned short *)(buf->b_data + 0x1fe) == cpu_to_le16(0xaa55)) {
		struct partition *first = (struct partition * )(buf->b_data + 0x1be);
		struct partition *entry = first;
		int saved_cylinders = param->cylinders;
		int num;
		unsigned char end_head, end_sec;

		for(num = 0; num < 4; num++) {
			end_head = entry->end_head;
			end_sec = entry->end_sector & 0x3f;

			if(end_head == 63) {
				param->heads = 64;
				param->sectors = 32;
				break;
			} else if(end_head == 127) {
				param->heads = 128;
				param->sectors = 32;
				break;
			} else if(end_head == 254) {
				param->heads = 255;
				param->sectors = 63;
				break;
			}
			entry++;
		}

		if (num == 4) {
			end_head = first->end_head;
			end_sec = first->end_sector & 0x3f;
		}

		param->cylinders = cap_to_cyls(capacity, param->heads * param->sectors);
		if (num < 4 && end_sec == param->sectors) {
			if (param->cylinders != saved_cylinders)
				dprintk((KERN_DEBUG "Adopting geometry: heads=%d, sectors=%d from partition table %d.\n",
					param->heads, param->sectors, num));
		} else if (end_head > 0 || end_sec > 0) {
			dprintk((KERN_DEBUG "Strange geometry: heads=%d, sectors=%d in partition table %d.\n",
				end_head + 1, end_sec, num));
			dprintk((KERN_DEBUG "Using geometry: heads=%d, sectors=%d.\n",
					param->heads, param->sectors));
		}
	}
	brelse(buf);
	return 0;
}

/**
 *	aac_queuedepth		-	compute queue depths
 *	@host:	SCSI host in question
 *	@dev:	SCSI device we are considering
 *
 *	Selects queue depths for each target device based on the host adapter's
 *	total capacity and the queue depth supported by the target device.
 *	A queue depth of one automatically disables tagged queueing.
 */

static void aac_queuedepth(struct Scsi_Host * host, struct scsi_device * dev )
{
	struct scsi_device * dptr;
	unsigned num = 0;
	unsigned depth;

	for(dptr = dev; dptr != NULL; dptr = dptr->next)
		if((dptr->host == host) && (dptr->type == 0))
			++num;

	dprintk((KERN_DEBUG "can_queue=%d num=%d\n", host->can_queue, num));
	if (num == 0)
		++num;
	depth = host->can_queue / num;
	if (depth > 255)
		depth = 255;
	else if (depth < 2)
		depth = 2;
	dprintk((KERN_DEBUG "aac_queuedepth.\n"));
	dprintk((KERN_DEBUG "Device #   Q Depth   Online\n"));
	dprintk((KERN_DEBUG "---------------------------\n"));
	for(dptr = dev; dptr != NULL; dptr = dptr->next)
	{
		if(dptr->host == host)
		{
			dptr->queue_depth = depth;
			dprintk((KERN_DEBUG "  %2d         %d        %d\n", 
				dptr->id, dptr->queue_depth, scsi_device_online(dptr)));
		}
	}
}

static int aac_ioctl(struct scsi_device *sdev, int cmd, void __user * arg)
{
	struct aac_dev *dev = (struct aac_dev *)sdev->host->hostdata;
	return aac_do_ioctl(dev, cmd, arg);
}

/**
 *	aac_eh_device_reset	-	Reset command handling
 *	@cmd:	SCSI command block causing the reset
 *
 *	Issue a reset of a SCSI device. We are ourselves not truely a SCSI
 *	controller and our firmware will do the work for us anyway. Thus this
 *	is a no-op. We just return FAILED.
 */

static int aac_eh_device_reset(struct scsi_cmnd *cmd)
{
	return FAILED;
}

/**
 *	aac_eh_bus_reset	-	Reset command handling
 *	@scsi_cmd:	SCSI command block causing the reset
 *
 *	Issue a reset of a SCSI bus. We are ourselves not truely a SCSI
 *	controller and our firmware will do the work for us anyway. Thus this
 *	is a no-op. We just return FAILED.
 */

static int aac_eh_bus_reset(struct scsi_cmnd* cmd)
{
	return FAILED;
}

/*
 *	aac_eh_reset	- Reset command handling
 *	@scsi_cmd:	SCSI command block causing the reset
 *
 */
static int aac_eh_reset(struct scsi_cmnd* cmd)
{
	struct scsi_device * dev = cmd->device;
	struct Scsi_Host * host = dev->host;
	struct scsi_cmnd * command;
	int count;
	struct aac_dev * aac;

	printk(KERN_ERR "%s: Host adapter reset request. SCSI hang ?\n", 
					AAC_DRIVERNAME);
	aac = (struct aac_dev *)host->hostdata;
	if (nblank(dprintk(x))) {
		int active = 0;

		active = active;
		dprintk((KERN_ERR
		  "%s: Outstanding commands on (%d,%d,%d,%d):\n",
		  AAC_DRIVERNAME,
		  host->host_no, dev->channel, dev->id, dev->lun));
		for(command = dev->device_queue; command; command = command->next)
		{
			if ((command->state != SCSI_STATE_FINISHED)
			 && (command->state != 0))
			dprintk((KERN_ERR
			  "%4d %c%c %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			  active++,
			  (command->SCp.phase == AAC_OWNER_FIRMWARE) ? 'A' : 'C',
			  (cmd == command) ? '*' : ' ',
			  command->cmnd[0], command->cmnd[1], command->cmnd[2],
			  command->cmnd[3], command->cmnd[4], command->cmnd[5],
			  command->cmnd[6], command->cmnd[7], command->cmnd[8],
			  command->cmnd[9]));
		}
	}

	if ((count = aac_check_health(aac)))
		return count;
	/*
	 * Wait for all commands to complete to this specific
	 * target (block maximum 60 seconds).
	 */
	for (count = 60; count; --count) {
		int active = aac->in_reset;
	
		if (active == 0)
		for (dev = host->host_queue; dev != (struct scsi_device *)NULL; dev = dev->next) {
			for(command = dev->device_queue; command; command = command->next) {
				if ((command != cmd)
				 && (command->SCp.phase == AAC_OWNER_FIRMWARE)) {
					++active;
					break;
				}
			}
		}
		/*
		 * We can exit If all the commands are complete
		 */
		if (active == 0)
			return SUCCESS;
#ifdef SCSI_HAS_HOST_LOCK
#if (!defined(CONFIG_CFGNAME))
		spin_unlock_irq(host->host_lock);
#else
		spin_unlock_irq(host->lock);
#endif
#else
		spin_unlock_irq(&io_request_lock);
#endif
		ssleep(1);
#ifdef SCSI_HAS_HOST_LOCK
#if (!defined(CONFIG_CFGNAME))
		spin_lock_irq(host->host_lock);
#else
		spin_lock_irq(host->lock);
#endif
#else
		spin_lock_irq(&io_request_lock);
#endif
	}
	printk(KERN_ERR "%s: SCSI bus appears hung\n", AAC_DRIVERNAME);
//	return -ETIMEDOUT;
	return SUCCESS; /* Cause an immediate retry of the command with a ten second delay after successful tur */
}

static int aac_sanity_check(struct scsi_device * sdev)
{
	return 0;
}

static void aac_poll(struct scsi_device * sdev)
{
	struct Scsi_Host * shost = sdev->host;
	struct aac_dev *dev = (struct aac_dev *)shost->hostdata;
	unsigned long flags;

#ifdef SCSI_HAS_HOST_LOCK
#if (!defined(CONFIG_CFGNAME))
	spin_lock_irqsave(shost->host_lock, flags);
#else
	spin_lock_irqsave(shost->lock, flags);
#endif
#else
	spin_lock_irqsave(&io_request_lock, flags);
#endif
	aac_adapter_intr(dev);
#ifdef SCSI_HAS_HOST_LOCK
#if (!defined(CONFIG_CFGNAME))
	spin_unlock_irqrestore(shost->host_lock, flags);
#else
	spin_unlock_irqrestore(shost->lock, flags);
#endif
#else
	spin_unlock_irqrestore(&io_request_lock, flags);
#endif
}

/**
 *	aac_cfg_open		-	open a configuration file
 *	@inode: inode being opened
 *	@file: file handle attached
 *
 *	Called when the configuration device is opened. Does the needed
 *	set up on the handle and then returns
 *
 *	Bugs: This needs extending to check a given adapter is present
 *	so we can support hot plugging, and to ref count adapters.
 */

static int aac_cfg_open(struct inode *inode, struct file *file)
{
	struct aac_dev *aac;
	unsigned minor_number = iminor(inode);
	int err = -ENODEV;

	list_for_each_entry(aac, &aac_devices, entry) {
		if (aac->id == minor_number) {
			file->private_data = aac;
			err = 0;
			break;
		}
	}

	return err;
}

/**
 *	aac_cfg_release		-	close down an AAC config device
 *	@inode: inode of configuration file
 *	@file: file handle of configuration file
 *	
 *	Called when the last close of the configuration file handle
 *	is performed.
 */
 
static int aac_cfg_release(struct inode * inode, struct file * file )
{
	return 0;
}

/**
 *	aac_cfg_ioctl		-	AAC configuration request
 *	@inode: inode of device
 *	@file: file handle
 *	@cmd: ioctl command code
 *	@arg: argument
 *
 *	Handles a configuration ioctl. Currently this involves wrapping it
 *	up and feeding it into the nasty windowsalike glue layer.
 *
 *	Bugs: Needs locking against parallel ioctls lower down
 *	Bugs: Needs to handle hot plugging
 */
 
static int aac_cfg_ioctl(struct inode *inode,  struct file *file,
		unsigned int cmd, unsigned long arg)
{
	return aac_do_ioctl(file->private_data, cmd, (void __user *)arg);
}


#define class_device Scsi_Host
#define class_to_shost(class_dev) class_dev

static ssize_t aac_show_model(struct class_device *class_dev,
		char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(class_dev)->hostdata;
	int len;

	if (dev->supplement_adapter_info.AdapterTypeText[0]) {
		char * cp = dev->supplement_adapter_info.AdapterTypeText;
		while (*cp && *cp != ' ')
			++cp;
		while (*cp == ' ')
			++cp;
		len = snprintf(buf, PAGE_SIZE, "%s\n", cp);
	} else
		len = snprintf(buf, PAGE_SIZE, "%s\n",
		  aac_drivers[dev->cardtype].model);
	return len;
}

static ssize_t aac_show_vendor(struct class_device *class_dev,
		char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(class_dev)->hostdata;
	int len;

	if (dev->supplement_adapter_info.AdapterTypeText[0]) {
		char * cp = dev->supplement_adapter_info.AdapterTypeText;
		while (*cp && *cp != ' ')
			++cp;
		len = snprintf(buf, PAGE_SIZE, "%.*s\n",
		  (int)(cp - (char *)dev->supplement_adapter_info.AdapterTypeText),
		  dev->supplement_adapter_info.AdapterTypeText);
	} else
		len = snprintf(buf, PAGE_SIZE, "%s\n",
		  aac_drivers[dev->cardtype].vname);
	return len;
}

static ssize_t aac_show_flags(struct class_device *class_dev, char *buf)
{
	int len = 0;

	if (nblank(dprintk(x)))
		len = snprintf(buf, PAGE_SIZE, "dprintk\n");
#	if (defined(AAC_DETAILED_STATUS_INFO))
		len += snprintf(buf + len, PAGE_SIZE - len,
		  "AAC_DETAILED_STATUS_INFO\n");
#	endif
	len += snprintf(buf + len, PAGE_SIZE - len, "SCSI_HAS_VARY_IO\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "SCSI_HAS_DUMP\n");
	return len;
}

static ssize_t aac_show_kernel_version(struct class_device *class_dev,
		char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(class_dev)->hostdata;
	int len, tmp;

	tmp = le32_to_cpu(dev->adapter_info.kernelrev);
	len = snprintf(buf, PAGE_SIZE, "%d.%d-%d[%d]\n", 
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  le32_to_cpu(dev->adapter_info.kernelbuild));
	return len;
}

static ssize_t aac_show_monitor_version(struct class_device *class_dev,
		char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(class_dev)->hostdata;
	int len, tmp;

	tmp = le32_to_cpu(dev->adapter_info.monitorrev);
	len = snprintf(buf, PAGE_SIZE, "%d.%d-%d[%d]\n", 
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  le32_to_cpu(dev->adapter_info.monitorbuild));
	return len;
}

static ssize_t aac_show_bios_version(struct class_device *class_dev,
		char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(class_dev)->hostdata;
	int len, tmp;

	tmp = le32_to_cpu(dev->adapter_info.biosrev);
	len = snprintf(buf, PAGE_SIZE, "%d.%d-%d[%d]\n", 
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  le32_to_cpu(dev->adapter_info.biosbuild));
	return len;
}

static ssize_t aac_show_serial_number(struct class_device *class_dev,
		char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(class_dev)->hostdata;
	int len = 0;

	if (le32_to_cpu(dev->adapter_info.serial[0]) != 0xBAD0)
		len = snprintf(buf, PAGE_SIZE, "%x\n",
		  le32_to_cpu(dev->adapter_info.serial[0]));
	return len;
}


/**
 *	aac_procinfo	-	Implement /proc/scsi/<drivername>/<n>
 *	@proc_buffer: memory buffer for I/O
 *	@start_ptr: pointer to first valid data
 *	@offset: offset into file
 *	@bytes_available: space left
 *	@host_no: scsi host ident
 *	@write: direction of I/O
 *
 *	Used to export driver statistics and other infos to the world outside 
 *	the kernel using the proc file system. Also provides an interface to
 *	feed the driver with information.
 *
 *		For reads
 *			- if offset > 0 return 0
 *			- if offset == 0 write data to proc_buffer and set the start_ptr to
 *			beginning of proc_buffer, return the number of characters written.
 *		For writes
 *			- writes currently not supported, return 0
 *
 *	Bugs:	Only offset zero is handled
 */
#define shost_to_class(shost) shost

static int aac_procinfo(
	char *proc_buffer, char **start_ptr,off_t offset,
	int bytes_available,
	int host_no,
	int write)
{
	struct aac_dev * dev = (struct aac_dev *)NULL;
	struct Scsi_Host * shost = (struct Scsi_Host *)NULL;
	char *buf;
	int len;
	int total_len = 0;

#if (defined(IOP_RESET))
	if(offset > 0)
#else
	if(write || offset > 0)
#endif
		return 0;
	*start_ptr = proc_buffer;
	list_for_each_entry(dev, &aac_devices, entry) {
		shost = dev->scsi_host_ptr;
		if (shost->host_no == host_no)
			break;
	}
	if (shost == (struct Scsi_Host *)NULL)
		return 0;
	dev = (struct aac_dev *)shost->hostdata;
	if (dev == (struct aac_dev *)NULL)
		return 0;
	if (!write) {
		buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!buf)
			return 0;
		len = snprintf(proc_buffer, bytes_available,
		  "Adaptec Raid Controller: %s\n", aac_driver_version);
		total_len += len;
		proc_buffer += len;
		len = aac_show_vendor(shost_to_class(shost), buf);
		len = snprintf(proc_buffer, bytes_available - total_len,
		  "Vendor: %.*s  ", len - 1, buf);
		total_len += len;
		proc_buffer += len;
		len = aac_show_model(shost_to_class(shost), buf);
		len = snprintf(proc_buffer, bytes_available - total_len,
		  "Model: %.*s", len, buf);
		total_len += len;
		proc_buffer += len;
		len = aac_show_flags(shost_to_class(shost), buf);
		if (len) {
			char *cp = proc_buffer;
			len = snprintf(cp, bytes_available - total_len,
			  "flags=%.*s", len, buf);
			total_len += len;
			proc_buffer += len;
			while (--len > 0) {
				if (*cp == '\n')
					*cp = '+';
				++cp;
			}
		}
		len = aac_show_kernel_version(shost_to_class(shost), buf);
		len = snprintf(proc_buffer, bytes_available - total_len,
		  "kernel: %.*s", len, buf);
		total_len += len;
		proc_buffer += len;
		len = aac_show_monitor_version(shost_to_class(shost), buf);
		len = snprintf(proc_buffer, bytes_available - total_len,
		  "monitor: %.*s", len, buf);
		total_len += len;
		proc_buffer += len;
		len = aac_show_bios_version(shost_to_class(shost), buf);
		len = snprintf(proc_buffer, bytes_available - total_len,
		  "bios: %.*s", len, buf);
		total_len += len;
		proc_buffer += len;
		len = aac_show_serial_number(shost_to_class(shost), buf);
		if (len) {
			len = snprintf(proc_buffer, bytes_available - total_len,
			  "serial: %.*s", len, buf);
			total_len += len;
		}
		kfree(buf);
		return total_len;
	}
#ifdef IOP_RESET
	{
		static char reset[] = "reset_host";
		if (strnicmp (proc_buffer, reset, sizeof(reset) - 1) == 0) {
			unsigned long flags;

			if (!capable(CAP_SYS_ADMIN))
				return bytes_available;
#ifdef SCSI_HAS_HOST_LOCK
#if (!defined(CONFIG_CFGNAME))
			spin_lock_irqsave(shost->host_lock, flags);
#else
			spin_lock_irqsave(shost->lock, flags);
#endif
#else
			spin_lock_irqsave(&io_request_lock, flags);
#endif
			(void)aac_reset_adapter(dev);
#ifdef SCSI_HAS_HOST_LOCK
#if (!defined(CONFIG_CFGNAME))
			spin_unlock_irqrestore(shost->host_lock, flags);
#else
			spin_unlock_irqrestore(shost->lock, flags);
#endif
#else
			spin_unlock_irqrestore(&io_request_lock, flags);
#endif
			return bytes_available;
		}
	}
#endif
	return 0;
}


static struct file_operations aac_cfg_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= aac_cfg_ioctl,
	.open		= aac_cfg_open,
	.release	= aac_cfg_release
};
static struct scsi_dump_ops aac_dump_ops = {
	.sanity_check	= aac_sanity_check,
	.poll		= aac_poll,
};

#define aac_driver_template aac_driver_template_dump.hostt
static struct SHT_dump aac_driver_template_dump = {
	.hostt = {
	.detect				= aac_detect,
	.module				= THIS_MODULE,
	.name           		= "AAC",
	.proc_name			= AAC_DRIVERNAME,
	.info           		= aac_info,
	.ioctl          		= aac_ioctl,
	.queuecommand   		= aac_queuecommand,
	.bios_param     		= aac_biosparm,	
	.proc_info      		= aac_procinfo,
	.eh_device_reset_handler	= aac_eh_device_reset,
	.eh_bus_reset_handler		= aac_eh_bus_reset,
	.eh_host_reset_handler		= aac_eh_reset,
	.can_queue      		= AAC_NUM_IO_FIB,	
	.this_id        		= MAXIMUM_NUM_CONTAINERS,
	.sg_tablesize   		= 16,
	.max_sectors    		= 128,
#if (AAC_NUM_IO_FIB > 256)
	.cmd_per_lun			= 256,
#else		
	.cmd_per_lun    		= AAC_NUM_IO_FIB, 
#endif	
	.use_new_eh_code		= 1, 
	.use_clustering			= ENABLE_CLUSTERING,
	.emulated                       = 1,
	.vary_io			= 1,
	.disk_dump			= 1,
},
	.dump_ops			= &aac_dump_ops,
};


static int __devinit aac_probe_one(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	unsigned index = id->driver_data;
	struct Scsi_Host *shost;
	struct aac_dev *aac;
	struct list_head *insert = &aac_devices;
	int error = -ENODEV;
	int unique_id = 0;

	list_for_each_entry(aac, &aac_devices, entry) {
		if (aac->id > unique_id)
			break;
		insert = &aac->entry;
		unique_id++;
	}

	error = pci_enable_device(pdev);
	if (error)
		goto out;
	error = -ENODEV;

	if (pci_set_dma_mask(pdev, DMA_32BIT_MASK) || 
			pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK))
		goto out;
	/*
	 * If the quirk31 bit is set, the adapter needs adapter
	 * to driver communication memory to be allocated below 2gig
	 */
	if (aac_drivers[index].quirks & AAC_QUIRK_31BIT) 
		if (pci_set_dma_mask(pdev, 0x7FFFFFFFULL) ||
				pci_set_consistent_dma_mask(pdev, 0x7FFFFFFFULL))
			goto out;
	
	pci_set_master(pdev);

	aac_driver_template.name = aac_drivers[index].name;
	shost = scsi_host_alloc(&aac_driver_template, sizeof(struct aac_dev));
	if (!shost)
		goto out_disable_pdev;

	shost->irq = pdev->irq;
	shost->base = pci_resource_start(pdev, 0);
	scsi_set_pci_device(shost, pdev);
	shost->unique_id = unique_id;
	/*
	 *	This function is called after the device list
	 *	has been built to find the tagged queueing
	 *	depth supported for each device.
	 */
	shost->select_queue_depths = aac_queuedepth;

	aac = (struct aac_dev *)shost->hostdata;
	aac->scsi_host_ptr = shost;	
	aac->pdev = pdev;
	aac->name = aac_driver_template.name;
	aac->id = shost->unique_id;
	aac->cardtype =  index;
	INIT_LIST_HEAD(&aac->entry);

	aac->fibs = kmalloc(sizeof(struct fib) * (shost->can_queue + AAC_NUM_MGT_FIB), GFP_KERNEL);
	if (!aac->fibs)
		goto out_free_host;
	spin_lock_init(&aac->fib_lock);

	/*
	 *	Map in the registers from the adapter.
	 */
	aac->base_size = AAC_MIN_FOOTPRINT_SIZE;
	if ((aac->regs.sa = ioremap(
	  (unsigned long)aac->scsi_host_ptr->base, AAC_MIN_FOOTPRINT_SIZE))
	  == NULL) {	
		printk(KERN_WARNING "%s: unable to map adapter.\n",
		  AAC_DRIVERNAME);
		goto out_free_fibs;
	}
	if ((*aac_drivers[index].init)(aac))
		goto out_unmap;

	/*
	 *	Start any kernel threads needed
	 */
	aac->thread_pid = kernel_thread((int (*)(void *))aac_command_thread,
	  aac, 0);
	if (aac->thread_pid < 0) {
		printk(KERN_ERR "aacraid: Unable to create command thread.\n");
		goto out_deinit;
	}

	/*
	 * If we had set a smaller DMA mask earlier, set it to 4gig
	 * now since the adapter can dma data to at least a 4gig
	 * address space.
	 */
	if (aac_drivers[index].quirks & AAC_QUIRK_31BIT)
		if (pci_set_dma_mask(pdev, DMA_32BIT_MASK))
			goto out_deinit;

	aac->maximum_num_channels = aac_drivers[index].channels;
	error = aac_get_adapter_info(aac);
	if (error < 0)
		goto out_deinit;

	/*
 	 * Lets override negotiations and drop the maximum SG limit to 34
 	 */
 	if ((aac_drivers[index].quirks & AAC_QUIRK_34SG) && 
			(aac->scsi_host_ptr->sg_tablesize > 34)) {
 		aac->scsi_host_ptr->sg_tablesize = 34;
 		aac->scsi_host_ptr->max_sectors = 128;
 	}

	/*
	 * Firware printf works only with older firmware.
	 */
	if (aac_drivers[index].quirks & AAC_QUIRK_34SG) 
		aac->printf_enabled = 1;
	else
		aac->printf_enabled = 0;
 
 	/*
	 * max channel will be the physical channels plus 1 virtual channel
	 * all containers are on the virtual channel 0
	 * physical channels are address by their actual physical number+1
	 */
	if (aac->nondasd_support == 1)
		shost->max_channel = aac->maximum_num_channels + 1;
	else
		shost->max_channel = 1;

	aac_get_config_status(aac);
	aac_get_containers(aac);
	list_add(&aac->entry, insert);

	shost->max_id = aac->maximum_num_containers;
	if (shost->max_id < aac->maximum_num_physicals)
		shost->max_id = aac->maximum_num_physicals;
	if (shost->max_id < MAXIMUM_NUM_CONTAINERS)
		shost->max_id = MAXIMUM_NUM_CONTAINERS;
	else
		shost->this_id = shost->max_id;

	/*
	 * dmb - we may need to move the setting of these parms somewhere else once
	 * we get a fib that can report the actual numbers
	 */
	shost->max_lun = AAC_MAX_LUN;

	pci_set_drvdata(pdev, shost);

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_deinit;
	scsi_scan_host(shost);

	return 0;

 out_deinit:
	kill_proc(aac->thread_pid, SIGKILL, 0);
	wait_for_completion(&aac->aif_completion);

	aac_send_shutdown(aac);
	aac_adapter_disable_int(aac);
	free_irq(pdev->irq, aac);
 out_unmap:
	fib_map_free(aac);
	pci_free_consistent(aac->pdev, aac->comm_size, aac->comm_addr, aac->comm_phys);
	kfree(aac->queues);
	iounmap(aac->regs.sa);
 out_free_fibs:
	kfree(aac->fibs);
	kfree(aac->fsa_dev);
 out_free_host:
	scsi_host_put(shost);
 out_disable_pdev:
	pci_disable_device(pdev);
 out:
	return error;
}

static void __devexit aac_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct aac_dev *aac = (struct aac_dev *)shost->hostdata;

	scsi_remove_host(shost);

	kill_proc(aac->thread_pid, SIGKILL, 0);
	wait_for_completion(&aac->aif_completion);

	aac_send_shutdown(aac);
	aac_adapter_disable_int(aac);
	fib_map_free(aac);
	pci_free_consistent(aac->pdev, aac->comm_size, aac->comm_addr,
			aac->comm_phys);
	kfree(aac->queues);

	free_irq(pdev->irq, aac);
	iounmap(aac->regs.sa);
	
	kfree(aac->fibs);
	kfree(aac->fsa_dev);
	
	list_del(&aac->entry);
	scsi_host_put(shost);
	pci_disable_device(pdev);
}

static struct pci_driver aac_pci_driver = {
	.name		= AAC_DRIVERNAME,
	.id_table	= aac_pci_tbl,
	.probe		= aac_probe_one,
	.remove		= __devexit_p(aac_remove_one),
};

static int aac_reboot_event(struct notifier_block * n, ulong code, void *p)
{
	if ((code == SYS_RESTART)
	 || (code == SYS_HALT)
	 || (code == SYS_POWER_OFF)) {
		struct aac_dev *aac;

		list_for_each_entry(aac, &aac_devices, entry)
			aac_send_shutdown(aac);
	}
	return NOTIFY_DONE;
}

static struct notifier_block aac_reboot_notifier =
{
	aac_reboot_event,
	NULL,
	0
};
 
static int __init aac_init(void)
{
	int error;
	
	printk(KERN_INFO "Adaptec %s driver (%s)\n",
	  AAC_DRIVERNAME, aac_driver_version);
	scsi_register_module(MODULE_SCSI_HA,&aac_driver_template);
	/* Reverse 'detect' action */
	aac_driver_template.present = 0;

	error = pci_register_driver(&aac_pci_driver);
	if (error < 0 || list_empty(&aac_devices)) {
		if (error >= 0) {
			pci_unregister_driver(&aac_pci_driver);
			error = -ENODEV;
		}
		scsi_unregister_module(MODULE_SCSI_HA,&aac_driver_template);
		return error;
	}

	aac_cfg_major = register_chrdev( 0, "aac", &aac_cfg_fops);
	if (aac_cfg_major < 0) {
		printk(KERN_WARNING
		       "aacraid: unable to register \"aac\" device.\n");
	}
#if (1) ? defined(__x86_64__) : defined(CONFIG_COMPAT)
	aac_ioctl32(FSACTL_MINIPORT_REV_CHECK, sys_ioctl);
	aac_ioctl32(FSACTL_SENDFIB, sys_ioctl);
	aac_ioctl32(FSACTL_OPEN_GET_ADAPTER_FIB, sys_ioctl);
	aac_ioctl32(FSACTL_GET_NEXT_ADAPTER_FIB,
	  aac_get_next_adapter_fib_ioctl);
	aac_ioctl32(FSACTL_CLOSE_GET_ADAPTER_FIB, sys_ioctl);
	aac_ioctl32(FSACTL_SEND_RAW_SRB, sys_ioctl);
	aac_ioctl32(FSACTL_GET_PCI_INFO, sys_ioctl);
	aac_ioctl32(FSACTL_QUERY_DISK, sys_ioctl);
	aac_ioctl32(FSACTL_DELETE_DISK, sys_ioctl);
	aac_ioctl32(FSACTL_FORCE_DELETE_DISK, sys_ioctl);
	aac_ioctl32(FSACTL_GET_CONTAINERS, sys_ioctl);
	aac_ioctl32(FSACTL_SEND_LARGE_FIB, sys_ioctl);
#endif
	register_reboot_notifier(&aac_reboot_notifier);

	return 0;
}

static void __exit aac_exit(void)
{
#if (1) ? defined(__x86_64__) : defined(CONFIG_COMPAT)
	unregister_ioctl32_conversion(FSACTL_MINIPORT_REV_CHECK);
	unregister_ioctl32_conversion(FSACTL_SENDFIB);
	unregister_ioctl32_conversion(FSACTL_OPEN_GET_ADAPTER_FIB);
	unregister_ioctl32_conversion(FSACTL_GET_NEXT_ADAPTER_FIB);
	unregister_ioctl32_conversion(FSACTL_CLOSE_GET_ADAPTER_FIB);
	unregister_ioctl32_conversion(FSACTL_SEND_RAW_SRB);
	unregister_ioctl32_conversion(FSACTL_GET_PCI_INFO);
	unregister_ioctl32_conversion(FSACTL_QUERY_DISK);
	unregister_ioctl32_conversion(FSACTL_DELETE_DISK);
	unregister_ioctl32_conversion(FSACTL_FORCE_DELETE_DISK);
	unregister_ioctl32_conversion(FSACTL_GET_CONTAINERS);
	unregister_ioctl32_conversion(FSACTL_SEND_LARGE_FIB);
#endif
	unregister_chrdev(aac_cfg_major, "aac");
	unregister_reboot_notifier(&aac_reboot_notifier);
	pci_unregister_driver(&aac_pci_driver);
	scsi_unregister_module(MODULE_SCSI_HA,&aac_driver_template);
}

module_init(aac_init);
module_exit(aac_exit);
EXPORT_NO_SYMBOLS;
