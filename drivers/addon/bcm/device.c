
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
 * device.c:  Modules to interface with the ubsec device.
 */
/*
 * Revision History:
 *
 * 10/xx/99 SOR Created.
 * 11/16/1999 DWP added PCI memory access enable for fixing linuxppc operation
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel  
 * May   2001 PW added selftest for bcmdiag
 * June  2001 SRM added per device testing and forced device failure. 
 * July  2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port
 */

#include "cdevincl.h"

//#define SET_PCI_MAX_LATENCY 0x80
//#define SET_PCI_CLS
#define SET_PCI_RETRY_COUNT 0x00
#define SET_PCI_TRDY_COUNT 0xff

/* Devices */
struct DeviceTable_s {
  short VendorID;
  short DeviceID;
};

/* Define supported device list. */
#ifdef UBSEC_582x_CLASS_DEVICE
#define NUM_SUPPORTED_DEVICE_TYPES 3
#else
#define NUM_SUPPORTED_DEVICE_TYPES 3
#endif

static struct DeviceTable_s DeviceList[NUM_SUPPORTED_DEVICE_TYPES]={
#ifndef UBSEC_582x_CLASS_DEVICE
  {BROADCOM_VENDOR_ID,BROADCOM_DEVICE_ID_5805},
  {BROADCOM_VENDOR_ID,BROADCOM_DEVICE_ID_5802},
  {BROADCOM_VENDOR_ID,BROADCOM_DEVICE_ID_5801}
#else
  {BROADCOM_VENDOR_ID,BROADCOM_DEVICE_ID_5821},
  {BROADCOM_VENDOR_ID,BROADCOM_DEVICE_ID_5822},
  {BROADCOM_VENDOR_ID,BROADCOM_DEVICE_ID_5820}
#endif
};

DeviceInfo_t DeviceInfoList[MAX_SUPPORTED_DEVICES];

void (*ubsec_callback)(void);

/*
 *
 * interrupt_proc
 *
 * This function receives the completion signal from the device.  At completion
 * time, it marks the time and schedules the bottom half.
 *
 */
void
interrupt_proc( int irq, void* pvoid, struct pt_regs *regs )
{
}

/*
 * Initialize and return all devices.
 * Return the number of initialized devices.
 */

#ifndef LINUX2dot2
extern struct pci_dev *globalpDev;
#endif

int 
InitDevices(int NumberOfCryptoMCRs,int NumberOfKeyMCRs)
{
  short value;
  ubsec_Status_t Status;
  int i;
  int NumDevices=0;
  DeviceInfo_pt pDevice=&DeviceInfoList[0];
  struct pci_dev* pDev=NULL;
  ubsec_chipinfo_io_t srl_chipinfo;

  if( !pcibios_present() ) {
    PRINTK("cryptonet:  No PCI bus detected!\n" );
    return( 0 ); /* XXX make an error message */
  }

  memset(&srl_chipinfo,0, sizeof(ubsec_chipinfo_io_t)); /* no chipinfo from SRL yet   */
  srl_chipinfo.Status =sizeof(ubsec_chipinfo_io_t);     /* Required for ubslib-SRL match check */

  /* Find and initialize all the devices we can */
  for (i=0; i < NUM_SUPPORTED_DEVICE_TYPES ; ) {
    /* Find the next device. */

#if 0
    PRINTK("Checking for %x-%x\n",DeviceList[i].VendorID,DeviceList[i].DeviceID);
#endif

    if ((pDev = pci_find_device(DeviceList[i].VendorID,DeviceList[i].DeviceID, pDev))==NULL) {
      if (NumDevices)
	break; /* Must not mix device types */
      i++;
      continue;
    }
#ifndef LINUX2dot2
 globalpDev = pDev;
#endif

#if 0 /* Taken care of below */
    /* set this device as a bus-mastering device */
    pci_set_master(pDev);
#endif

    /* below paragraph added 16-nov-1999 dwp to make work on linuxppc */
    pci_read_config_word( pDev, PCI_COMMAND, &value ); /* SOR added */
    if( !(value & PCI_COMMAND_MEMORY) )    {
      value |= PCI_COMMAND_MEMORY;
      pci_write_config_word( pDev, PCI_COMMAND, value );
      pci_read_config_word( pDev, PCI_COMMAND, &value );
      if( !(value & PCI_COMMAND_MEMORY ))   {
	PRINTK( " memory access enable failed\n" );
      }
    }
    if( !(value & PCI_COMMAND_MASTER) ) {
      value |= PCI_COMMAND_MASTER;
      pci_write_config_word( pDev, PCI_COMMAND, value );
      pci_read_config_word( pDev, PCI_COMMAND, &value );
      if( !(value & PCI_COMMAND_MASTER ))  {
	PRINTK( " bus master enable failed\n" );
      }
    }	
#ifdef SET_PCI_MAX_LATENCY
    pci_write_config_byte(pDev,PCI_LATENCY_TIMER,SET_PCI_MAX_LATENCY);
#endif
#ifdef SET_PCI_CLS // Set the cache line size
    pci_write_config_byte(pDev,PCI_CACHE_LINE_SIZE,1);
#endif
#ifdef SET_PCI_RETRY_COUNT
    pci_write_config_byte(pDev,0x41,SET_PCI_RETRY_COUNT);
#endif
#ifdef SET_PCI_TRDY_COUNT
    pci_write_config_byte(pDev,0x40,SET_PCI_TRDY_COUNT);
#endif



    /* let the outside world know what we found */
    printk( " <BCM%04X, Bus %i, Slot %d, ", pDev->device, pDev->bus->number, PCI_SLOT(pDev->devfn) );
    printk( "IRQ %d> ", pDev->irq );
    sema_init (&pDevice->Semaphore_SRL, 1);

    Status=ubsec_InitDevice(pDev->device,
#ifndef LINUX2dot2
			    pci_resource_start(pDev,0),
#else
			    pDev->base_address[0], 
#endif
			    pDev->irq,
			    NumberOfCryptoMCRs,
			    NumberOfKeyMCRs, &(pDevice->Context),(OS_DeviceInfo_t) pDevice);

    printk("\n");
    if (Status != UBSEC_STATUS_SUCCESS) {
      PRINTK("InitDevice Failed (%ld)\n",Status);
      continue;
    }

    pDevice->pDev=pDev;

    Status=Selftest(pDevice->Context,pDev);

    pDevice->DeviceStatus = Status;

#if 0
    if (Status != UBSEC_STATUS_SUCCESS) {
      PRINTK("Device failed %lx\n",Status);
    } else {
#endif

    if (!srl_chipinfo.Features && ((Status=ubsec_chipinfo(pDevice->Context, (void *)&srl_chipinfo)) != 0)) {
      PRINTK("ubsec_chipinfo() failed (%ld)\n",Status);
      continue;
    }

    /* Check to determine device and stepping */
    pDevice->Features = ~(0); /* Start with all features enabled */

    pci_read_config_word( pDev, PCI_CLASS_REVISION, &value );
    if ((value & 0x00E0) == 0xE0) {
      /* 3DES, ARC4 command not supported in export mode */
      pDevice->Features &= ~(UBSEC_EXTCHIPINFO_ARC4);
      pDevice->Features &= ~(UBSEC_EXTCHIPINFO_3DES);
    }
    if (pDev->device < BROADCOM_DEVICE_ID_5821) {
      /* ARC4 command not supported by BCM580x devices */
      pDevice->Features &= ~(UBSEC_EXTCHIPINFO_ARC4);
      /* DblModExp command not supported by BCM580x devices */
      pDevice->Features &= ~(UBSEC_EXTCHIPINFO_DBLMODEXP);
    }
    if (pDev->device == BROADCOM_DEVICE_ID_5821) {
      /* BCM5821 */
      if ((value & 0x001F) == 0x01) {
	/* Rev A0; RNG, ARC4_CONT not supported */
	pDevice->Features &= ~(UBSEC_EXTCHIPINFO_RNG);
	pDevice->Features &= ~(UBSEC_EXTCHIPINFO_ARC4);
      }
    }
    pDevice->Features &= srl_chipinfo.Features;

    NumDevices++;
    if (NumDevices == MAX_SUPPORTED_DEVICES)
      break;
    pDevice++;
    
#if 0
    }
#endif
    
  } /* For each detected CryptoNet device in the system */

  return(NumDevices);
}

/*
 * dump_pci_config: Dump the contents of the pci configuration associated
 * with the device.
 */
void
dump_pci_config(struct pci_dev *pDev)
{
  int i;
  int j;
  unsigned char uc;

  printk("  --------------- PCI CONFIG REGISTERS ---------------");

  for( i = 0; i < 65; i += 4 ) {
    if( !(i%16) ) {
      printk( "\n  " );
    }
    printk( "--%02d:", i/4);
    for( j = i+3; j >= i; j-- ) {
      pci_read_config_byte( pDev, j, &uc );
      printk( "%02X", uc );
    }
  }
  /* printk( "\n  " ); */

  printk("\n  ----------------------------------------------------\n");
}

/*
 * Use an Intermediate ISR callback because of
 * differences in parameters. For ISR and callback
 * instead of saving the SRL functions we just call
 * them directly.
 */
void
inter_callback(int irq,void *param,struct pt_regs *regs)
{
  DeviceInfo_pt pDevice=(DeviceInfo_pt)param;
  if (pDevice->Context==0)
    return;
  ubsec_ISR(pDevice->Context);
}

void
LinuxAllocateIRQ(int irq,void *context)
{
  request_irq(irq,inter_callback, SA_INTERRUPT | SA_SHIRQ, UBSEC_DEVICE_NAME,(void *)context); 
}

void
LinuxFreeIRQ(int irq, void *context)
{
  free_irq(irq,context);
}

/*
 * Redirection of callback to make it fit
 * 
 */
void
callback_dpc(void *param)
{
  DeviceInfo_pt pDevice = (DeviceInfo_pt) param;
  if (pDevice->Context==0)
    return;
  ubsec_ISRCallback(pDevice->Context);
}

/*
 * Schedule it for a local function
 */
int
LinuxScheduleCallback(void *Context, DeviceInfo_pt pDevice)
{
  pDevice->completion_handler_task.routine = callback_dpc;
  pDevice->completion_handler_task.data =pDevice;

    /* queue up the handler in the bottom half */
  queue_task( &pDevice->completion_handler_task, &tq_immediate );
  mark_bh( IMMEDIATE_BH );
  return 0;

}

/*
 * LinuxInitCriticalSection:
 */
void
LinuxInitCriticalSection(volatile DeviceInfo_pt pDevice)
{
  sema_init(&pDevice->Semaphore_SRL, 1);
}

/*
 * LinuxEnterCriticalSection:
 */
unsigned long
LinuxEnterCriticalSection(DeviceInfo_pt pDevice)
{
  down_interruptible(&pDevice->Semaphore_SRL);
  return 0;
}

/*
 * LinuxLeaveCriticalSection: Called by SRL
 */
void
LinuxLeaveCriticalSection(DeviceInfo_pt pDevice)
{
  up (&pDevice->Semaphore_SRL);
}

/*
 * LinuxTestCriticalSection:
 */
unsigned long
LinuxTestCriticalSection(DeviceInfo_pt pDevice)
{
  if (down_trylock (&pDevice->Semaphore_SRL))
    return -1;
  return 0;
}

void
LinuxWaitus( int wait_us )
{
  udelay(wait_us);
}

int
TestDevice(int SelectedDevice)
{
  
  int status = 1;
  volatile static int flag = 0;

#ifdef DEBUG_FAILOVER
  PRINTK("Entering TestDevice, SelectedDevice = %d, DeviceStatus = %d\n", 
	 SelectedDevice, DeviceInfoList[SelectedDevice].DeviceStatus);
#endif

  if(flag == 0xdeadbeef) {
    
#ifdef DEBUG_FAILOVER
    PRINTK("Returning from TestDevice, because of flag.\n");
#endif

    return 1;

  } else {

    flag = 0xdeadbeef;
    /* should we clear the semaphore here ????? */
    if(DeviceInfoList[SelectedDevice].DeviceStatus != -1) {
       DeviceInfoList[SelectedDevice].DeviceStatus = -1; /* remove from the active list */
      status = Selftest(DeviceInfoList[SelectedDevice].Context, DeviceInfoList[SelectedDevice].pDev);
      SetDeviceStatus(&DeviceInfoList[SelectedDevice], status);
    } else {
#ifdef DEBUG_FAILOVER
      PRINTK("TestDevice is resetting device %d whose status was %d and will be %d.\n", 
	      SelectedDevice, DeviceInfoList[SelectedDevice].DeviceStatus, status);
#endif
      /* keep device failing but next time it'll pass and status will be cleared */
      ubsec_ResetDevice(DeviceInfoList[SelectedDevice].Context);
      SetDeviceStatus(&DeviceInfoList[SelectedDevice], status);
    }
    
    flag = 0;
  }
  
  return DeviceInfoList[SelectedDevice].DeviceStatus;
}

int
TestDevices(PInt pm)
{
  int Retval=0;
  int i, timestogo;
  int seldev = *pm;

#ifdef DEBUG_FAILOVER
  PRINTK("Entering TestDevices, seldev = %d\n", seldev);
#endif

  if (seldev > NumDevices) {
    PRINTK("Selected Device is not installed.\n");
    return (NumDevices);
  }
  if (seldev > 0) {
      seldev--;
      timestogo = 1;
  } else
      timestogo = NumDevices;
  
  /* test all the devices or a given device. */
  for (i=seldev; i < seldev+timestogo ; i++) {
    Retval=TestDevice(i);
    if (Retval)
      return (-1);
  }
  return(Retval);
}

int
FailDevices(PInt pm)
{
  int i;
  int seldev = *pm;

#ifdef DEBUG_FAILOVER
  if(seldev) { 
    PRINTK("Entering FailDevice, will fail device %d whose status is %d.\n", 
	   seldev - 1, DeviceInfoList[seldev-1].DeviceStatus);
  } else {
    PRINTK("Entering FailDevice, will fail all devices\n", 
	   seldev - 1, DeviceInfoList[seldev-1].DeviceStatus);
  }
#endif

  if (seldev > NumDevices) {
    PRINTK("Selected device is not installed.\n");
    return (-1);
  }
  if (seldev > 0) {
     DeviceInfoList[seldev-1].DeviceStatus = -1;
     ubsec_DisableInterrupt(DeviceInfoList[seldev-1].Context);
  } else {
    /* Fail all the devices. */
    for (i=0; i < NumDevices ; i++) {
      DeviceInfoList[i].DeviceStatus = -1;
      ubsec_DisableInterrupt(DeviceInfoList[i].Context);
    }
  }
  
  return(0);
}

int
DumpDeviceInfo(PInt pm)
{
  int i, timestogo;
  int seldev = *pm;

  if (seldev > NumDevices) {
    PRINTK("Selected device is not installed.\n");
    return (-1);
  }
  if (seldev > 0) {
      seldev--;
      timestogo = 1;
  } else
      timestogo = NumDevices;
  /* Dump Info for all the devices or a given device. */
  for (i=seldev; i < seldev+timestogo ; i++) {
    dump_pci_config(DeviceInfoList[i].pDev);
    ubsec_DumpDeviceInfo(DeviceInfoList[i].Context);
  }
  return(0);
}

/*
 * Get hardware version:
 * Return the ID (lower byte) and revision ID
 */
int
GetHardwareVersion(PInt pm)
{
  unsigned char uc;
  unsigned short retval;
  DeviceInfo_pt pDevice;
  int DNum = *pm;

  if (DNum > NumDevices) {
    PRINTK("Selected device is not installed.\n");
    return (-1);
  }
  if (DNum > 0)
    DNum--;

  pDevice=&DeviceInfoList[DNum];

  pci_read_config_byte( pDevice->pDev, PCI_REVISION_ID, &uc );
  retval=(((pDevice->pDev->device)&0xff)<<8); /* Only need the lower byte */
  retval+=uc;
  return(retval);
}
