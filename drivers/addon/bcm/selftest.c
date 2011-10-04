
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
 * selftest.c: Character driver interface to selftest routines in the ubsec driver
 *
 * SOR
 * JJT
 * RJT
 */

#include "cdevincl.h"

static void CryptoCallback(unsigned long CommandContext,long Result);
static void KeyCallback(unsigned long CommandContext,long Result);

static long Status;
static int CallbackStatus=0;

/*
 *
 */
int
Selftest(ubsec_DeviceContext_t pContext,struct pci_dev* pDev)
{
  unsigned long			delay_total_us;

  CallbackStatus=0; /* inc'd on callback */

  Status=ubsec_TestCryptoDevice(pContext,CryptoCallback,(unsigned long)pDev);
  if (Status != UBSEC_STATUS_SUCCESS) {
    printk("Selftest Device failed %lx\n",Status);
    /* RJT call ubsec_PollDevice() here? Chip seems lost after failing */
    goto Return;
  }

  for (delay_total_us=1  ; !CallbackStatus ; delay_total_us++) {
#ifdef POLL /* We need to poll the device if we are operating in POLL mode. */
    ubsec_PollDevice(pContext);
#endif
    if (delay_total_us >= 3000000) {
      Status=UBSEC_STATUS_TIMEOUT;
      break;
    }
    udelay(1);
  }

  if (Status != UBSEC_STATUS_SUCCESS)
     goto Return;

  if(UBSEC_IS_CRYPTO_DEVICEID(pDev->device)) {
    goto Return;
  }

  CallbackStatus=0; /* inc'd on callback */

  Status=ubsec_TestKeyDevice(pContext,KeyCallback,(unsigned long)pDev);
  if (Status != UBSEC_STATUS_SUCCESS) {
    printk("Key Selftest Device failed %lx\n",Status);
    /* RJT call ubsec_PollDevice() here? Chip seems lost after failing */
    goto Return;
  }

  for (delay_total_us=1  ; !CallbackStatus ; delay_total_us++) {
#ifdef POLL /* We need to poll the device if we are operating in POLL mode. */
    ubsec_PollDevice(pContext);
#endif
    if (delay_total_us >= 3000000) {
      Status=UBSEC_STATUS_TIMEOUT;
      break;
    }
    udelay(1);
  }

 Return:
        ubsec_ResetDevice(pContext);
	return(Status);
}


static void CryptoCallback(unsigned long CommandContext,long Result)
{
  struct pci_dev *pDev=(struct pci_dev *) CommandContext; /* This is our handle to the pci device */
  if (Status != UBSEC_STATUS_SUCCESS)
     return;
  if (Result!=UBSEC_STATUS_SUCCESS) 
    printk("Device crypto selftest <%i-%i,%x> failed (%d)\n",
	   pDev->bus->number, PCI_SLOT(pDev->devfn),pDev->device,(unsigned int)Result);
  

  CallbackStatus++;
  Status=Result;
}

static void KeyCallback(unsigned long CommandContext,long Result)
{
  struct pci_dev *pDev=(struct pci_dev *) CommandContext; /* This is our handle to the pci device */
  if (Status != UBSEC_STATUS_SUCCESS)
     return;
  if (Result!=UBSEC_STATUS_SUCCESS) 
    printk("Device key selftest <%i-%i,%x> failed (%d)\n",
	   pDev->bus->number, PCI_SLOT(pDev->devfn),pDev->device,(unsigned int)Result);
  

  CallbackStatus++;
  Status=Result;
}





