/************************************************************************/
/* File pcifr_proc.c created by Allan Trautman on Thu Aug  2 2001.      */
/************************************************************************/
/* Supports the ../proc/ppc64/pcifr for the pci flight recorder.        */
/* Copyright (C) 20yy  <Allan H Trautman> <IBM Corp>                    */
/*                                                                      */
/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation; either version 2 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */ 
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */ 
/* along with this program; if not, write to the:                       */
/* Free Software Foundation, Inc.,                                      */ 
/* 59 Temple Place, Suite 330,                                          */ 
/* Boston, MA  02111-1307  USA                                          */
/************************************************************************/
#include <stdarg.h>
#include <linux/kernel.h>

#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/time.h>

#include <linux/pci.h>
#include <asm/pci-bridge.h>
#include <linux/netdevice.h>

#include <asm/flight_recorder.h>
#include <asm/iSeries/iSeries_pci.h>
#include "pci.h"

void pci_Fr_TestCode(void);

static spinlock_t proc_pcifr_lock;
struct flightRecorder* PciFr = NULL;

extern long Pci_Interrupt_Count;
extern long Pci_Event_Count;
extern long Pci_Io_Read_Count;
extern long Pci_Io_Write_Count;
extern long Pci_Cfg_Read_Count;
extern long Pci_Cfg_Write_Count;
extern long Pci_Error_Count;

/************************************************************************/
/* Forward declares.                                                    */
/************************************************************************/
static struct proc_dir_entry *pciFr_proc_root = NULL;
int proc_pciFr_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data);
static struct proc_dir_entry *pciDev_proc_root = NULL;
int proc_pciDev_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data);

/************************************************************************/
/* Create entry ../proc/ppc64/pcifr                                     */
/************************************************************************/
void proc_pciFr_init(struct proc_dir_entry *proc_ppc64_root)
{
	if (proc_ppc64_root == NULL) return;

	/* Read = User,Group,Other, Write User */
	printk("PCI: Creating ../proc/ppc64/pcifr \n");
	spin_lock(&proc_pcifr_lock);
	pciFr_proc_root = create_proc_entry("pcifr", S_IFREG | S_IRUGO | S_IWUSR, proc_ppc64_root);
	spin_unlock(&proc_pcifr_lock);

	if (pciFr_proc_root == NULL) return;

	pciFr_proc_root->nlink = 1;
	pciFr_proc_root->data = (void *)0;
	pciFr_proc_root->read_proc  = proc_pciFr_read_proc;
	pciFr_proc_root->write_proc = NULL;

	PciFr = alloc_Flight_Recorder(NULL,"PciFr", 4096);

	printk("PCI: Creating ../proc/ppc64/pci \n");
	spin_lock(&proc_pcifr_lock);
	pciDev_proc_root = create_proc_entry("pci", S_IFREG | S_IRUGO | S_IWUSR, proc_ppc64_root);
	spin_unlock(&proc_pcifr_lock);

	if (pciDev_proc_root == NULL) return;

	pciDev_proc_root->nlink = 1;
	pciDev_proc_root->data = (void *)0;
	pciDev_proc_root->read_proc  = proc_pciDev_read_proc;
	pciDev_proc_root->write_proc = NULL;
}

/*******************************************************************************/
/* Read function for ../proc/ppc64/pcifr.                                      */
/*  -> Function grabs a copy of the pcifr(could change) and writes the data to */
/*     the caller.  Note, it may not all fit in the buffer.                   */
/* Tip:                                                                        */
/* ./fs/proc/generic.c::proc_file_read is the caller of this routine.          */
/*******************************************************************************/
int proc_pciFr_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *PciFrBuffer;
	int PciFrBufLen;

	PciFrBuffer = (char*)kmalloc(PciFr->Size, GFP_KERNEL);
	if (PciFrBuffer == NULL)
		return -ENOMEM;

	/* Snapshot the pci flight recorder */
	PciFrBufLen = fr_Dump(PciFr, PciFrBuffer, PciFr->Size);

        if (off >= PciFrBufLen) {
                *eof = 1;
                kfree(PciFrBuffer);
                return 0;
        }
        if (off)
                PciFrBufLen -= off;
        if (PciFrBufLen > count)
                PciFrBufLen = count;
        else
                *eof = 1;

        memcpy (page, PciFrBuffer + off, PciFrBufLen);
        *start = page;
        kfree(PciFrBuffer);
        return PciFrBufLen;
}

/*******************************************************************************/
/* Build Device Buffer for /proc/ppc64/pci                                     */
/*******************************************************************************/
static int build_PciDev_Buffer(char *ProcBuffer, int BufferSize) 
{
	int BufLen  = 0;
	struct pci_dev*    PciDev;		    /* Device pointer              */
	struct net_device* dev;		            /* net_device pointer          */
	int    DeviceCount  = 0;
	int j;

	BufLen += sprintf(ProcBuffer+BufLen,"Pci I/O Reads. %8ld  ",Pci_Io_Read_Count);
	BufLen += sprintf(ProcBuffer+BufLen,"Pci I/O Writes %8ld\n",Pci_Io_Write_Count);

	BufLen += sprintf(ProcBuffer+BufLen,"Pci Cfg Reads. %8ld  ",Pci_Cfg_Read_Count);
	BufLen += sprintf(ProcBuffer+BufLen,"Pci Cfg Writes %8ld\n",Pci_Cfg_Write_Count);

	BufLen += sprintf(ProcBuffer+BufLen,"Pci I/O Errors %8ld\n",Pci_Error_Count);
	BufLen += sprintf(ProcBuffer+BufLen,"\n");

	/***************************************************************************/
	/* List the devices                                                        */
	/***************************************************************************/
	pci_for_each_dev(PciDev) {
		if ( BufLen > BufferSize-128) {    /* Room for another line?       */
			BufLen +=sprintf(ProcBuffer+BufLen,"Buffer Full\n");
			break;
		}
		if( PCI_SLOT(PciDev->devfn) != 0) {
			++DeviceCount;
			BufLen += sprintf(ProcBuffer+BufLen,"%3d. ",DeviceCount);
			if ( PciDev->sysdata != NULL ) {
				BufLen += format_device_location(PciDev,ProcBuffer+BufLen,128);
			}
			else {
				BufLen += sprintf(ProcBuffer+BufLen,"No Device Node!\n");
			}
			BufLen += sprintf(ProcBuffer+BufLen,"\n");

			/* look for the net devices out */
			for (dev = dev_base; dev != NULL; dev = dev->next) 	{
                               
				if (!dev->base_addr) /* virtual device, no base address */
					break;
				
				for (j=0;j<6;j++) { /* PCI has 6 base addresses */
					if (dev->base_addr == PciDev->resource[j].start ) {
						BufLen += sprintf(ProcBuffer+BufLen, "     - Net device: %s\n", dev->name);
						break;
					} /* if */
				}
				if (j!=6) break; /* found one */
			} /* for */
		} /* if(PCI_SLOT(PciDev->devfn) != 0)  */
	}
	return BufLen;
}
/*******************************************************************************/
/* Get called when client reads the ../proc/ppc64/pci.                         */
/*******************************************************************************/
int proc_pciDev_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *ProcBuffer;
	int ProcBufSize;

	ProcBuffer = (char*)kmalloc(4096, GFP_KERNEL);
	if (ProcBuffer == NULL)
		return -ENOMEM;
	ProcBufSize = build_PciDev_Buffer(ProcBuffer, 4096);

	if (off >= ProcBufSize) {
		*eof = 1;
		kfree(ProcBuffer);
		return 0;
	}
	if (off)
		ProcBufSize -= off;
	if (ProcBufSize > count)
		ProcBufSize = count;
	else
		*eof = 1;

	memcpy (page, ProcBuffer + off, ProcBufSize);
	*start = page;
	kfree(ProcBuffer);
	return ProcBufSize;
}
