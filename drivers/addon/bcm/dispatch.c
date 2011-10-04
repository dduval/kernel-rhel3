
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
 * dispatch.c: Character driver interface to the ubsec driver
 */
/* Revision History:
 *
 * May   2000 SOR Created
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel 
 * May   2001 PW added selftest for bcmdiag
 * May   2001 SRM added support for dumping statistics to the proc
 *   		  file system
 * May   2001 SRM change stats file name to bcm5820 
 * May   2001 SRM Move snmp related stuff to snmp.c. Also support
 *		  statistics thru' snmp for linux kernel less than
 *		  2 2 17.
 * June  2001 SRM Added per device testing and forced device failure. 
 * July  2001 RJT Added support for BCM5821
 * Dec   2001 SRM stats ioctl
 */

#define MODULE

#define NUMBER_OF_KEY_MCRS 128
#define NUMBER_OF_CRYPTO_MCRS 512 




#include "cdevincl.h"

#ifdef BCM_OEM_1
#include "bcm_oem_1.h"
#endif

char kernel_version[] = UTS_RELEASE;

#undef  KERN_DEBUG
#define KERN_DEBUG "<1>"

static int ubsec_ioctl(struct inode *inode, struct file *filp,unsigned int cmd, unsigned long arg);

static int ubsec_open(struct inode *inode, struct file *filp);

static int ubsec_release(struct inode *inode, struct file *filp);

#ifdef DVT
DVT_Params_t DVTparams;
extern unsigned long Page_Size;
extern int power_of_2(unsigned long);
extern unsigned long next_smaller_power_of_2(unsigned long);
#endif /* DVT */

void get_ubsec_Function_Ptrs(ubsec_Function_Ptrs_t *fptrs); /* for function ptrs access */

int PInt_Contents;
int NumDevices=0;
ubsec_chipinfo_io_t   ExtChipInfo;
int UserCopySize;

int 
GetDeviceStatus(DeviceInfo_t Device)
{ 
  return Device.DeviceStatus;
}

int 
SetDeviceStatus(DeviceInfo_pt pDevice, int Status)
{ 
  return pDevice->DeviceStatus = Status;
}

unsigned short Version=0x0181; /* Upper byte is major, lower byte is Minor */
static char *version_string="%s driver v%x.%02x";

int ubsec_major_number = -1;
static int SelectedDevice=0;
/*
 *  UBSEC's set of file operations.  This is the data structure that
 *  will be passed when we call register_chrdev().
 */
#ifndef LINUX2dot2
static struct file_operations ubsec_file_ops = {
 
        owner:          THIS_MODULE,
        ioctl:          ubsec_ioctl,
        open:           ubsec_open,
        release:        ubsec_release,
};
 
#else       
struct file_operations ubsec_file_ops = {
  NULL,          /* lseek   */
  NULL,          /* read    */
  NULL,          /* write   */
  NULL,          /* readdir */
  NULL,          /* select  */
  ubsec_ioctl,/* ioctl   */
  NULL,          /* mmap    */
  ubsec_open,          /* open    */
  NULL,          /* flush    */
  ubsec_release,          /* release */
  NULL           /* fsync   */
  /* fill any remaining entries with NULL */
};

#endif

/**************************************************************************
 *
 *  Function:  ubsec_ioctl
 *
 *************************************************************************/
static int 
ubsec_ioctl(struct inode *inode,struct file *filp,unsigned int cmd, unsigned long arg)
{

  long			Retval=0;

  int                   status = 0;
  int                   deadlockctr = 0;
  
  unsigned short        value;

  
  
  /* Simple round robin scheduling of device. We need to increment
     first since the keysetup command may block. */
  
 TheBeginning:

  deadlockctr = 0;
  do {

    /* For diagnostic related stuff do not try any available devices */
    /* Try the intended device or all devices as directed by the command */

    if (cmd >= UBSEC_DEVICEDUMP || cmd == UBSEC_SELFTEST || cmd == UBSEC_FAILDEVICE) {
      break;
    }

    if(++deadlockctr == (NumDevices * 2)) {  /* to be conservative... */
#ifdef DEBUG_FAILOVER
      PRINTK("dispatch found no more devices.\n");
#endif
      return 1; /* error: no more devices */
    }

    if ((++SelectedDevice) == NumDevices)
      SelectedDevice=0;

  } while(GetDeviceStatus(DeviceInfoList[SelectedDevice]));

#ifdef DEBUG_FAILOVER
  printk("\n");
  PRINTK("dsptch-pre: SltdDev=%d,DevStati=%d %d\n", 
	 SelectedDevice, DeviceInfoList[0].DeviceStatus, DeviceInfoList[1].DeviceStatus);
#endif


  switch(cmd) {
#ifdef BCM_OEM_1
  case BCM_OEM_1_IOCTL1:
	BCM_OEM1_IOCTL1_HANDLER();
	break;
  case BCM_OEM_1_IOCTL2:
	BCM_OEM1_IOCTL2_HANDLER();
	break;
#endif /* BCM_OEM_1 */

  case UBSEC_ENCRYPT_DECRYPT_FUNC:
    status = do_encrypt(DeviceInfoList[SelectedDevice].Context, 
			(void *)arg, DeviceInfoList[SelectedDevice].Features);
    break;

  case UBSEC_KEY_SETUP_FUNC:
    status = ubsec_keysetup(DeviceInfoList[SelectedDevice].Context, (void *)arg);
    break;

  case UBSEC_MATH_FUNC:
    status = ubsec_math(DeviceInfoList[SelectedDevice].Context, (void *)arg);
    break;

  case UBSEC_RNG_FUNC:
    if (DeviceInfoList[SelectedDevice].Features & UBSEC_EXTCHIPINFO_RNG)
      status = ubsec_rng(DeviceInfoList[SelectedDevice].Context, (void *)arg);
    else
      status = UBSEC_STATUS_NO_DEVICE;
    break;
    
  case UBSEC_TLS_HMAC_FUNC:
    status = ubsec_tlsmac(DeviceInfoList[SelectedDevice].Context, (void *)arg);
    break;
    
  case UBSEC_SSL_MAC_FUNC:
    status = ubsec_sslmac(DeviceInfoList[SelectedDevice].Context, (void *)arg);
    break;
    
  case UBSEC_SSL_HASH_FUNC:
    status = ubsec_hash(DeviceInfoList[SelectedDevice].Context, (void *)arg);
    break;

  case UBSEC_SSL_DES_FUNC:
    status = ubsec_sslcipher(DeviceInfoList[SelectedDevice].Context, (void *)arg,
			     DeviceInfoList[SelectedDevice].Features);
    break;

  case UBSEC_SSL_ARC4_FUNC:
    if (DeviceInfoList[SelectedDevice].Features & UBSEC_EXTCHIPINFO_ARC4)
      status = ubsec_sslarc4(DeviceInfoList[SelectedDevice].Context, (void *)arg);
    else
      status = UBSEC_STATUS_NO_DEVICE;
    break;

  case UBSEC_CHIPINFO_FUNC:
    status = obsolete_chipinfo(DeviceInfoList[SelectedDevice].Context, (void *)arg);
    break;

  case UBSEC_STATS_FUNC:
    {
      ubsec_stats_io_t IOInfo;
      int device_num;
      copy_from_user((void *) &IOInfo,(void *) arg, sizeof(ubsec_stats_io_t)); 
      device_num  = IOInfo.device_num;


	if ( (device_num >= NumDevices) || (device_num < 0) )
		return -1;

      ubsec_GetStatistics(DeviceInfoList[device_num].Context, &IOInfo.dev_stats);
      copy_to_user((void *) arg, (void *) &IOInfo, sizeof(ubsec_stats_io_t));
    }
    break;
    
  case UBSEC_EXTCHIPINFO_FUNC:
    copy_from_user((void *)&ExtChipInfo, (void *)arg, sizeof(ubsec_chipinfo_io_t)); 
    if (ExtChipInfo.Status !=sizeof(ubsec_chipinfo_io_t)) {
      UserCopySize = sizeof(ubsec_chipinfo_io_t);
      if (UserCopySize > ExtChipInfo.Status)
	UserCopySize = ExtChipInfo.Status;
      ExtChipInfo.Status = UBSEC_STATUS_NO_DEVICE;
      copy_to_user((void *)arg, (void *)&ExtChipInfo, UserCopySize);
      return(-1);
    }
    else if ((ExtChipInfo.CardNum >= NumDevices) || (ExtChipInfo.CardNum < 0)) {
      ExtChipInfo.CardNum = NumDevices; 
      ExtChipInfo.Status = UBSEC_STATUS_INVALID_PARAMETER; 
    }
    else {
      status = ubsec_chipinfo(DeviceInfoList[ExtChipInfo.CardNum].Context, &ExtChipInfo); 
      ExtChipInfo.NumDevices = NumDevices; 
      ExtChipInfo.Features &= DeviceInfoList[ExtChipInfo.CardNum].Features;
      ExtChipInfo.Status = UBSEC_STATUS_SUCCESS; 
    }
    copy_to_user((void *)arg, (void *)&ExtChipInfo, sizeof(ubsec_chipinfo_io_t)); 
    if (ExtChipInfo.Status != UBSEC_STATUS_SUCCESS)
      return(-1);
    else
      return(0);
    break;
    
  case UBSEC_DEVICEDUMP:
    copy_from_user((void *)&PInt_Contents, (void *)arg, sizeof(int));
    Retval=DumpDeviceInfo((PInt)&PInt_Contents);
    if (Retval)
      return(-1); /* Error */
    break;

  case UBSEC_FAILDEVICE:
    copy_from_user((void *)&PInt_Contents, (void *)arg, sizeof(int));
    Retval=FailDevices((PInt)&PInt_Contents);
    if (Retval)
      return(-1); /* Error */
    break;

  case UBSEC_SELFTEST:
#ifdef BCM_OEM_1
	DISABLE_BCM_OEM1();
#endif /*BCM_OEM_1 */
    copy_from_user((void *)&PInt_Contents, (void *)arg, sizeof(int));
    Retval=TestDevices((PInt)&PInt_Contents);
#ifdef BCM_OEM_1
	ENABLE_BCM_OEM1();
#endif /*BCM_OEM_1 */
    if (Retval)
      return (Retval); /* Error */

  case UBSEC_GETVERSION:
    copy_from_user((void *)&PInt_Contents, (void *)arg, sizeof(int));
    Retval=GetHardwareVersion((PInt)&PInt_Contents); /* For the moment one card */
    Retval=Retval<<16;
    Retval+=Version;
    #ifdef LINUX_IA64
    return Retval;
    #else
    return(-Retval);
    #endif
    break;

  case UBSEC_GETNUMCARDS:
	copy_to_user((void *)arg,&NumDevices,sizeof(int));
    return NumDevices;


  case  UBSEC_GET_FUNCTION_PTRS:
	{
	ubsec_Function_Ptrs_t fptrs;
	get_ubsec_Function_Ptrs(&fptrs);
      	copy_to_user((void *) arg, (void *) &fptrs, sizeof(ubsec_Function_Ptrs_t));
	}
    break;

#ifdef DVT 
  case UBSEC_RESERVED:
    copy_from_user((void *)&DVTparams, (void *)arg, sizeof(DVT_Params_t));
    if ((DVTparams.CardNum >= NumDevices) || (DVTparams.CardNum < 0)) {
      {PRINTK("Invalid CardNum (%d), must be 0",DVTparams.CardNum);}
      if (NumDevices == 1)
	printk("\n");
      else
	printk("-%d\n",NumDevices-1);
      return -1; 
    }
    switch (DVTparams.Command) {
    case UBSEC_DVT_PAGESIZE: /* Wrapper command */
      DVTparams.OutParameter = Page_Size;
      if (!DVTparams.InParameter) {
	DVTparams.OutParameter = Page_Size = PAGE_SIZE;
	DVTparams.Status = UBSEC_STATUS_SUCCESS;
	Retval = UBSEC_STATUS_SUCCESS;
      }      
      else if ((DVTparams.InParameter > PAGE_SIZE) ||
	       (DVTparams.InParameter < 2)) {
	DVTparams.Status = UBSEC_STATUS_INVALID_PARAMETER;
      }
      else {
	if (!power_of_2(DVTparams.InParameter))
	  DVTparams.InParameter = next_smaller_power_of_2(DVTparams.InParameter);
	DVTparams.OutParameter = Page_Size; 
	Page_Size = DVTparams.InParameter; 
	DVTparams.Status = UBSEC_STATUS_SUCCESS;
	Retval = UBSEC_STATUS_SUCCESS;
      }
      break;
    default:
      /* Pass all other commands down to the SRL */
      Retval=ubsec_dvt_handler((void *)DeviceInfoList[DVTparams.CardNum].Context,(void *)&DVTparams); 
    };
    copy_to_user((void *)arg, (void *)&DVTparams, sizeof(DVT_Params_t));
    return(Retval);
    break;
#endif /* DVT */

  default:
    return -EINVAL;
  }
  
#ifdef DEBUG_FAILOVER
  PRINTK("dsptch-pst: SltdDev=%d,DevStati=%d %d\n", 
	 SelectedDevice, DeviceInfoList[0].DeviceStatus, DeviceInfoList[1].DeviceStatus);
  if((status == ETIMEDOUT) || (status == -ETIMEDOUT)) {
    PRINTK("dispatch.c: TIMED OUT SelectedDevice=%d, DeviceStatus=%d\n", 
	   SelectedDevice, DeviceInfoList[SelectedDevice].DeviceStatus);
  }
#endif

  switch(status) {
  case 0:
    break;

  case (ETIMEDOUT):
    status = -ETIMEDOUT;
  case (-ETIMEDOUT):
    DeviceInfoList[SelectedDevice].DeviceStatus = TestDevice(SelectedDevice);
    /*  goto TheBeginning; */
    return(status);
    break;
    
  default:
    /*  goto TheBeginning; */
    return(status);
    break;
  }
  
  return 0;
}


/**************************************************************************
 *
 *  Function:  init_module
 *   
 *************************************************************************/
int init_module(void)
{
  printk(version_string,UBS_DEVICE_TYPE,Version>>8,Version&0xff);

#ifdef DEBUG
  printk(" (SRL v%d.%x%c):\n",UBSEC_VERSION_MAJOR,UBSEC_VERSION_MINOR,UBSEC_VERSION_REV);
#else
  printk(":\n");
#endif

/*
 * First try to find and initialize the ubsec devices.
 */
 if ((NumDevices=InitDevices(NUMBER_OF_CRYPTO_MCRS,NUMBER_OF_KEY_MCRS)) ==0) {
   PRINTK(KERN_DEBUG "Device startup failed\n");
   return -ENOMEM;
 }

#ifdef BCM_OEM_1
	INIT_BCM_OEM_1();
#endif

 if (init_keyif() < 0) {
   PRINTK(KERN_DEBUG "no memory for key buffer\n");
   return(ENOMEM);
 }

 if (init_mathif() < 0) {
   PRINTK(KERN_DEBUG "no memory for mathif buffer\n");
   shutdown_keyif();
   return(ENOMEM);
 }

 if (init_rngif() < 0) {
   PRINTK(KERN_DEBUG "no memory for rng buffer\n");
   shutdown_keyif();
   shutdown_mathif();
   return(ENOMEM);
 }

 if (init_cryptoif() < 0) {
   PRINTK(KERN_DEBUG "crypto init failed\n");
   shutdown_keyif();
   shutdown_mathif();
   return(ENOMEM);
 }

 if(init_arc4if() < 0) {
   PRINTK(KERN_DEBUG "ssl init failed\n");
   shutdown_cryptoif();
   shutdown_rngif();
   shutdown_keyif();
   shutdown_mathif();
   return(ENOMEM);
 }


 /* create a /proc/net/ubsec entry for possible SNMP support */
#if (defined(UBSEC_STATS) && defined(CONFIG_PROC_FS))
  init_snmp_stats_support();
#endif

  /*
   *  Register the device -- ask for a dnynamicly assigned major number
   */
 ubsec_major_number = register_chrdev(0, UBSEC_KEYDEVICE_NAME, &ubsec_file_ops);
 if(ubsec_major_number < 0 ) /* Bound? */
   return(ubsec_major_number);

#if 0 
register_chrdev(ubsec_major_number, UBSEC_KEYDEVICE_NAME, &ubsec_file_ops);
#endif

 EXPORT_NO_SYMBOLS;
 return 0; /* success */
}

/**************************************************************************
 *
 *  Function:  cleanup_module
 *
 *************************************************************************/
void
cleanup_module(void)
{
  int i;

#ifdef BCM_OEM_1
	SHUTDOWN_BCM_OEM_1();
#endif /* BCM_OEM_1 */

  shutdown_keyif();
  shutdown_mathif();
  shutdown_rngif();
  shutdown_cryptoif();
  shutdown_arc4if();
#if (defined(UBSEC_STATS) && defined(CONFIG_PROC_FS))
  shutdown_snmp_stats_support();
#endif


#if 0
  unregister_chrdev(ubsec_major_number, UBSEC_DEVICE_NAME);
#endif
  unregister_chrdev(ubsec_major_number, UBSEC_KEYDEVICE_NAME);

  /* Shutdown all the devices. */
  for (i=0; i < NumDevices ; i++)
    ubsec_ShutdownDevice(DeviceInfoList[i].Context); /* Shutdown the device */
  PRINTK("Module unloaded\n");
}

/*
 * ubsec_open:
 */
static int
ubsec_open(struct inode *inode, struct file *filp)
{
  MOD_INC_USE_COUNT;
  return 0;
}

/*
 * ubsec_release:
 */
static int
ubsec_release(struct inode *inode, struct file *filp)
{
  MOD_DEC_USE_COUNT;
  return 0; 
}

/*
* get_ubsec_Function_Ptrs 
* copies the function ptrs for DeviceInfoList, crypto,RNG and DMA Memory access
*/

void get_ubsec_Function_Ptrs(ubsec_Function_Ptrs_t *fptrs)
{

extern void * Linux_AllocateDMAMemory(ubsec_DeviceContext_t *context, int size);
extern void  Linux_FreeDMAMemory(void * virtual , int size);

	fptrs->PhysDeviceInfoList_Ptr = DeviceInfoList;
	fptrs->OS_AllocateDMAMemory_Fptr = Linux_AllocateDMAMemory;
	fptrs->OS_FreeDMAMemory_Fptr = Linux_FreeDMAMemory;
	fptrs->ubsec_InitHMACState_Fptr = ubsec_InitHMACState;
	fptrs->ubsec_CipherCommand_Fptr = ubsec_CipherCommand;
	fptrs->ubsec_RNGCommand_Fptr = ubsec_RNGCommand;

	return;
}

MODULE_LICENSE("GPL");
