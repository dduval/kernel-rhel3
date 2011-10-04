/*****************************************************************************
 *
 *  MegaRacDrvrLx.c : MegaRac device driver for Linux
 *
 *  VisualStudio printing format: courier, 12, landscape
 *
 ****************************************************************************/

#include <linux/module.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/tqueue.h>
#include <linux/unistd.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>

#include "MegaRacDrvr.h"
#include "MegaRacDrvrLx.h"
#include "MegaRacDLL.h"

/* This is defined in 2.2.x series linux kernels, but not in 2.4.x */
/* andrewm@ami.com 6/8/2001 */
#ifndef copy_from_user_ret
#define copy_from_user_ret(to,from,n,retval) ({ if (copy_from_user(to,from,n)) return retval; })
#endif

/* Added some macros for proper compilation on different versions of linux */
/* andrewm@ami.com 6/18/2001 */
#define PCI_BASE resource[0].start
#warning wrong

MODULE_AUTHOR("American Megatrends Inc");
MODULE_LICENSE("GPL");
#if   (OEM == MEGARAC_OEM_AMI)
#define MR_REGION_NAME "MegaRAC"
MODULE_DESCRIPTION("AMI MegaRAC driver");
#elif (OEM == MEGARAC_OEM_DELL)
#define MR_REGION_NAME "DRAC"
MODULE_DESCRIPTION("DELL Remote Assistant Card driver");
#else
#define MR_REGION_NAME "RAC"
MODULE_DESCRIPTION("Remote Assistant Card driver");
#endif

typedef enum {
	MR_IO_IDLE,
	MR_IO_WRITE_OUT,
	MR_IO_WRITE_COMPLETE
} MR_IO;

typedef struct _RAC_OS_INFO {
	struct _RAC_INFO *pRacInfo;
	struct pci_dev pciDev;
	struct timer_list timerUOW;
	struct tq_struct isrBHqueue;
	unsigned char *pContiguousBuffer;
	unsigned int contiguousBufferLength;

	/* Different handling of wait queues in 2.4.x series kernels */
	/* andrewm@ami.com 6/18/2001 */
	wait_queue_head_t writeQ, readQ, ioctlQ;
	MEGARAC_IO_BUFS ioCB;
	volatile MR_IO ioState;
	BOOL bIRQ, bChrDev, bRegion;
	RAC_EVENT_NOTIFICATION events;
} RAC_OS_INFO;

typedef struct _MEGARAC_DEVICE_INFO {
	RAC_INFO racInfo;
	RAC_OS_INFO osInfo;
} MEGARAC_DEVICE_INFO;

static MEGARAC_DEVICE_INFO devInfo;
static const char regionName[] = MR_REGION_NAME;
static const unsigned long regionExtent = 0x0080;
static int racDebugFlag = 0;	/* modify at load time with insmod command */
static int racSupport = 0;	/* disable cursor support for Dell */

#define RAC_MAJOR 170		/* officially designated as Linux device major 170 per hpa@zytor.com */
#define RAC_MINOR 0		/* ...see /usr/doc/kernel-doc-.../devices.txt */

/*----------------------------------------------------------------------------
 *  
 *--------------------------------------------------------------------------*/
#if _DEBUG
#define DEBUG_PRINT		/* turn debug printing on */
#define DEBUG_PRINT_FUNCTION megaOutputFunc
#endif

#include "MegaRacDebug.h"

DebugFlagDeclare(mega, static);
DebugOutputFuncLX(mega, static);

/*----------------------------------------------------------------------------
 *  macros required to support MegaRacDrvr.c
 *--------------------------------------------------------------------------*/

/* use PCI API here I think */
#define MALLOC_CONTIGUOUS(raci,ptr,cast,cnt) ptr=(cast)kmalloc(cnt,GFP_ATOMIC)
#define   FREE_CONTIGUOUS(raci,ptr)                    kfree(ptr)

#define VIRTUAL_TO_PHYSICAL(raci,physAddr,virtAddr) physAddr = (void*)virt_to_phys(virtAddr);

/* Modified macro works with new and old versions of gcc */
/* andrewm@ami.com 6/18/2001 */
#define  READ_RAC_UCHAR( raci,addr)         inb (     (unsigned long)raci addr )
#define  READ_RAC_ULONG( raci,addr)         inl (     (unsigned long)raci addr )
#define WRITE_RAC_UCHAR(raci,addr,val)      outb( val,(unsigned long)raci addr )
#define WRITE_RAC_ULONG(raci,addr,val)      outl( val,(unsigned long)raci addr )

/*****************************************************************************
 *    
 *   The linux kernel makes a feeble attempt to limit shared access
 *   to i/o ports by the check_region() function.
 *   If check_region() is used by this driver, a error return will
 *   occur because the 'vga+' driver has already reserved the ports 
 *   we want to access.
 *   Currently the kernel does not actually modify the 
 *   "x86 TSS I/O Permissions Bit Map", so we can skip using
 *   check_region() and request_region(), and just blindly 
 *   access the ports.  However, if in the future the kernel
 *   does start checking the TSS then the following code is
 *   a starting point for a work around.
 *      typedef int (*sysfun_p)();
 *      extern long sys_call_table[];
 *      int error;
 *      void *kfunc;
 *      kfunc=(void*)sys_call_table[__NR_ioperm]; //sys_ioperm 101
 *      error=((sysfun_p)kfunc)(0x3d4,2,1);
 *
 ****************************************************************************/
static unsigned short
getHardwareCursor(void)
{
	unsigned int save3D4, highByte, lowByte;
	unsigned long flags = 0;	/* =0 makes compiler happy */

	save_flags(flags);
	cli();

	save3D4 = inb(0x3d4);
	outb(0x0e, 0x3d4);
	highByte = inb(0x3d5);
	highByte &= 0x00ff;
	outb(0x0f, 0x3d4);
	lowByte = inb(0x3d5);
	lowByte &= 0x00ff;
	outb(save3D4, 0x3d4);

	restore_flags(flags);

	return ((highByte << 8) | lowByte);
}				/* end of getHardwareCursor() */

/*****************************************************************************
 *
 ****************************************************************************/
static void
issueEvent(RAC_INFO * pRAC, RAC_EVENT rawEvent, RAC_EVENT firmEvent)
{
	RAC_OS_INFO *pOSI = pRAC->pOsInfo;

	DebugFlagPrint(mega, MEGA_EVENT,
		       ("issueEvent    : rawEvent=%#x, firmEvent=%#x, count=%d\n",
			rawEvent, firmEvent, pOSI->events.eventHandle[racEventFirmwareRequest]));

	if (rawEvent == racEventFirmwareRequest) {
		pOSI->events.eventHandle[firmEvent] = 1;	/* this event has occurred */
		pOSI->events.eventHandle[racEventFirmwareRequest]++;	/* this is our 'dirty' flag */
	}

	/* Same command, different arguments on different kernels */
	/* andrewm@ami.com 6/18/2001 */
	wake_up_interruptible(&pOSI->ioctlQ);

}				/* end of issueEvent() */

/*****************************************************************************
 *
 ****************************************************************************/
static int
racOpenDev(struct inode *ip, struct file *fp)
{

	DebugFlagPrint(mega, MEGA_ENTRY, ("racOpenDev    : ip=%p, fp=%p <<<<<<<<<<<\n", ip, fp));
	fp->private_data = &devInfo;
	MOD_INC_USE_COUNT;

	return 0;
}				/* end of racOpenDev() */

/*****************************************************************************
 *
 ****************************************************************************/
static int
racCloseDev(struct inode *ip, struct file *fp)
{

	DebugFlagPrint(mega, MEGA_ENTRY, ("racCloseDev   : ip=%p, fp=%p >>>>>>>>>>>\n", ip, fp));
	fp->private_data = NULL;
	MOD_DEC_USE_COUNT;

	return 0;
}				/* end of racCloseDev() */

/*****************************************************************************
 *
 ****************************************************************************/
static int
racPciInit(MEGARAC_DEVICE_INFO * pMDI)
{
	static void racISR(int irq, void *dev_id, struct pt_regs *regs);
	static int racRead(struct file *, char *, size_t, loff_t *);
	static int racWrite(struct file *, const char *, size_t, loff_t *);
	static int racIoctl(struct inode *, struct file *, unsigned int, unsigned long);

	//andrewm@ami.com
	static struct file_operations racFops = {
		/* 2.4.x series kernels include an owner field in this struct */
		/* It should always be the THIS_MODULE macro for our uses.    */
		/* andrewm@ami.com 6/18/2001                                  */
		owner:THIS_MODULE,
		llseek:NULL,
		read:racRead,
		write:racWrite,
		readdir:NULL,
		poll:NULL,
		ioctl:racIoctl,
		mmap:NULL,
		open:racOpenDev,
		flush:NULL,
		release:racCloseDev
	};

	RAC_INFO *pRAC = &pMDI->racInfo;
	RAC_OS_INFO *pOSI = &pMDI->osInfo;
	struct pci_dev *pPCI = &pOSI->pciDev;
	int result;

	{
		struct pci_dev *tempDev;
		tempDev = pci_find_device(AMI_VENDOR_ID, AMI_MEGA_RAC_ID, NULL);
		if (tempDev == NULL) {
			printk(KERN_ERR "%s not found on PCI bus\n", regionName);
			return (-ENODEV);
		}

		*pPCI = *tempDev;

		DebugFlagPrint(mega, MEGA_ENTRY,
			       ("racPciInit    : found MegaRac: base=%#lx, irq=%d ==========\n",
				pci_resource_start(pPCI, 0), pPCI->irq));
	}

	/* Get IO region.... */

	/* Changed PCI base references to a macro based on kernel version */
	/* andrewm@ami.com 6/18/2001 */
	result = check_region(pci_resource_start(pPCI, 0), regionExtent);
	if (result) {
		printk(KERN_ERR "%s can't get reserved region %#lx\n", regionName, pci_resource_start(pPCI, 0));
		return result;
	}

	/* Changed PCI base references to a macro based on kernel version */
	/* andrewm@ami.com 6/18/2001 */
	request_region(pci_resource_start(pPCI, 0), regionExtent, regionName);
	pOSI->bRegion = TRUE;

	/* disable any interrupt from the MegaRac until ready */

	/* Changed PCI base references to a macro based on kernel version */
	/* andrewm@ami.com 6/18/2001 */
	racSetAddrs(pRAC, (void *) pci_resource_start(pPCI, 0));
	#warning wrong type

	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DISABLE_ALL);
	WRITE_RAC_UCHAR(pRAC->, portAddrHCR, HCR_INTR_RESET);

	/* register device with kernel */

	result = register_chrdev(RAC_MAJOR, regionName, &racFops);
	if (result < 0) {
		printk(KERN_ERR "%s can't get reserved major device %d\n", regionName, RAC_MAJOR);
		return result;
	}
	pOSI->bChrDev = TRUE;

	/* attach the IRQ to the driver */

	result = request_irq(pPCI->irq, racISR, SA_SHIRQ, regionName, pMDI);
	if (result) {
		printk(KERN_ERR "%s can't register IRQ\n", regionName);
		return result;
	}
	pOSI->bIRQ = TRUE;

	/* all done */

	return 0;
}				/* end of racPciInit() */

/*****************************************************************************
 *  module entry point for driver
 ****************************************************************************/
int
init_module(void)
{
	extern void cleanup_module(void);
	static void racTimeout(unsigned long);
	static void racIsrBottomHalf(void *);
	MEGARAC_DEVICE_INFO *pMDI = &devInfo;
	RAC_INFO *pRAC = &pMDI->racInfo;
	RAC_OS_INFO *pOSI = &pMDI->osInfo;
	int retval;

	DebugFlagSet(mega, racDebugFlag);	/* turn on debugging */

	if (!pci_present()) {
		racDebugFlag++;	/* prevent compiler warning */
		printk(KERN_ERR "PCI BIOS not present\n");
		return -ENODEV;
	}

	/* Initialize wait queue heads */
	/* Kernels before 2.4.x don't use wait queue heads in the same way */
	/* andrewm@ami.com 6/18/2001 */

	init_waitqueue_head(&pOSI->readQ);
	init_waitqueue_head(&pOSI->writeQ);
	init_waitqueue_head(&pOSI->ioctlQ);

	/* allocate resources, ISR's, etc. */
	pMDI->osInfo.pRacInfo = &pMDI->racInfo;
	pMDI->racInfo.pOsInfo = &pMDI->osInfo;

	pOSI->isrBHqueue.routine = racIsrBottomHalf;
	pOSI->isrBHqueue.data = pMDI;

	pOSI->contiguousBufferLength = max(MAX_DMI_BUFFER, MOUSE_DATA_BUFFER_SIZE);
	pOSI->contiguousBufferLength += sizeof(CCB_Header);
	pOSI->pContiguousBuffer = kmalloc(pOSI->contiguousBufferLength, GFP_KERNEL);
	#warning PCI DMA ?
	if (pOSI->pContiguousBuffer == NULL)
		return -ENOMEM;

	retval = racPciInit(pMDI);
	if (retval) {
		DebugFlagPrint(mega, MEGA_ENTRY, ("init_module    :  Error initializing %#x\n", retval));
		cleanup_module();
		return retval;
	}

	/* register special support functions for use by the common driver code */

	if (racSupport & MEGARAC_SUPPORT_CURSOR)
		racGetHardwareCursor = getHardwareCursor;	/* work around for ASIC bug */

	racOsEventProc = issueEvent;

	/* Tell card it's ready */

	racStartupFinal(pRAC);	/* interrupts are now active */

	/* setup timer for racTimeout function */

	init_timer(&pOSI->timerUOW);
	pOSI->timerUOW.function = racTimeout;
	pOSI->timerUOW.data = (unsigned long) pMDI;
	pOSI->timerUOW.expires = jiffies + HZ;	/* one second */
	add_timer(&pOSI->timerUOW);	/* start timer */

	return 0;
}				/* end of init_module() */

/*****************************************************************************
 *
 ****************************************************************************/
void
cleanup_module(void)
{
	MEGARAC_DEVICE_INFO *pMDI = &devInfo;
	RAC_INFO *pRAC = &pMDI->racInfo;
	RAC_OS_INFO *pOSI = &pMDI->osInfo;
	int result;

	if (pOSI->pContiguousBuffer) {
		kfree(pOSI->pContiguousBuffer);
		pOSI->pContiguousBuffer = NULL;
	}

	if (pOSI->timerUOW.function) {
		pOSI->timerUOW.data = 0;	/* avoid race condition with racTimeout() */
		del_timer_sync(&pOSI->timerUOW);
		pOSI->timerUOW.function = NULL;
	}

	if (pOSI->bIRQ) {
		free_irq(pOSI->pciDev.irq, pMDI);
		pOSI->bIRQ = FALSE;
	}

	if (pOSI->bChrDev) {
		result = unregister_chrdev(RAC_MAJOR, regionName);
		pOSI->bChrDev = FALSE;
		if (result)
			DebugFlagPrint(mega, MEGA_ENTRY, ("cleanup_module: unregister_chrdev=%#x\n", result));
	}

	if (pOSI->bRegion) {
		/* Changed PCI base references to a macro based on kernel version */
		/* andrewm@ami.com 6/18/2001 */
		release_region(pci_resource_start(&pOSI->pciDev, 0), regionExtent);
		pOSI->bRegion = FALSE;
	}

	racShutdownBegin(pRAC);

}				/* end of cleanup_module() */

/*****************************************************************************
 *
 ****************************************************************************/
static void
racTimeout(unsigned long context)
{
	MEGARAC_DEVICE_INFO *pMDI = (MEGARAC_DEVICE_INFO *) context;
	RAC_INFO *pRAC = &pMDI->racInfo;
	RAC_OS_INFO *pOSI = &pMDI->osInfo;

	if (pOSI->timerUOW.data == 0)	/* avoid race condition with cleanup_module() */
		return;
	racTimerTick(pRAC);

	pOSI->timerUOW.expires = jiffies + HZ;	/* one second */
	add_timer(&pOSI->timerUOW);	/* restart the timer */
	#warning mod_timer maybe ?

}				/* end of racTimeout() */

/*********************************************************
 *
 *********************************************************/
static int
racWrite(struct file *fp, const char *pData, size_t dataLen, loff_t * ignored)
{
	RAC_INFO *pRAC = fp->private_data;
	RAC_OS_INFO *pOSI = pRAC->pOsInfo;
	CCB_Header *pCCB = (CCB_Header *) pOSI->pContiguousBuffer;
	MEGARAC_IO_BUFS *pIOB = &pOSI->ioCB;
	RAC_DRVR_ERR drvrStatus;
	int osStatus;

	/* Declare our own wait queue */
	/* andrewm@ami.com 6/18/2001  */
	DECLARE_WAITQUEUE(wait, current);

	DebugFlagPrint(mega, MEGA_DEVICE,
		       ("racWrite      : fp=%p, pData=%p, dataLen=%#lx, loff=%p, ioState=%d\n",
			fp, pData, dataLen, ignored, pOSI->ioState));

	/* validate the request */

	if (pData == NULL || dataLen != sizeof(*pIOB)) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racWrite      : unknown data %p, %#x\n", pData, dataLen));
		return -ENOTTY;
	}

	/* Add the current context to the wait queue and set ourself as interruptible */
	/* andrewm@ami.com 6/18/2001 */

	add_wait_queue(&pOSI->writeQ, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (pOSI->ioState != MR_IO_IDLE) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racWrite      : waiting\n"));

		if (signal_pending(current)) {
			/* Set ourself back to running mode and take ourself off the wait queue */
			/* andrewm@ami.com 6/18/2001 */
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&pOSI->writeQ, &wait);
			return -ERESTARTSYS;
		}

		/* Go to sleep waiting for a wakeup from read */
		/* andrewm@ami.com 6/18/2001 */
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);

	}

	/* We don't need to wait any more, so set ourselves back to running, */
	/* and remove ourselves from the wait queue */
	/* andrewm@ami.com 6/18/2001 */
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&pOSI->writeQ, &wait);

	/* construct control structure in kernelspace */

	copy_from_user_ret(pIOB, (void *) pData, sizeof(*pIOB), -EFAULT);

	DebugFlagPrint(mega, MEGA_DEVICE,
		       ("racWrite      : ioCC=%#lx, reqBuf=%p, reqLen=%#lx, respBuf=%p, respLen=%#lx\n",
			pIOB->ioControlCode, pIOB->requestBuf, pIOB->requestBufLen,
			pIOB->responseBuf, pIOB->responseBufLen));

	/* construct ccb in kernelspace */

	if (pOSI->pContiguousBuffer == NULL ||
	    pOSI->contiguousBufferLength < max(pIOB->requestBufLen, pIOB->responseBufLen))
		return -EFAULT;

	copy_from_user_ret(pCCB, pIOB->requestBuf, pIOB->requestBufLen, -EFAULT);

	/* do some work */

	racPreProcessCCB(pRAC, pCCB, pCCB->Length, pIOB->ioControlCode, &drvrStatus);
	switch (drvrStatus) {
	case racDrvrErrNone:
		osStatus = 0;
		break;
	case racDrvrErrPending:
		osStatus = -EWOULDBLOCK;
		break;
	case racDrvrErrInvalidParameter:
		osStatus = -EMSGSIZE;
		break;
	case racDrvrErrNotImplemented:
		osStatus = -ENOSYS;
		break;
	default:
		osStatus = -ENOTTY;
		break;
	}

	if (pIOB->ioControlCode == IOCTL_GET_GRAPHICS) {
		osStatus = -ENOSYS;	/* not needed, and not supported */
	}

	else if (drvrStatus == racDrvrErrNone) {	/* completely handled within driver  */
		pOSI->ioState = MR_IO_WRITE_COMPLETE;	/* ...don't need to send to card */
	}

	else if (drvrStatus == racDrvrErrPending) {	/* need to send request to the card */
		pOSI->ioState = MR_IO_WRITE_OUT;	/* indicate write is outstanding */

		if (!racSendCcb(pRAC, pCCB, pIOB->ioControlCode)) {
			pOSI->ioState = MR_IO_IDLE;	/* failed: leave osStatus as set in switch */
		} else {	/* ccb has been sent to megarac */

			osStatus = 0;	/* allow API write() to complete normally */
		}
	}

	DebugFlagPrint(mega, MEGA_DEVICE,
		       ("racWrite      : exit drvrStatus=%#x, osStatus=%d, len=%#x\n",
			drvrStatus, osStatus, sizeof(CCB_Header) + pCCB->Length));

	return osStatus ? osStatus : dataLen;

}				/* end of racWrite() */

/*****************************************************************************
 *
 ****************************************************************************/
static void
racISR(int irq, void *dev_id, struct pt_regs *regs)
{
	MEGARAC_DEVICE_INFO *pMDI = (MEGARAC_DEVICE_INFO *) dev_id;
	RAC_INFO *pRAC = &pMDI->racInfo;
	unsigned char hifr;

	hifr = READ_RAC_UCHAR(pRAC->, portAddrHIFR);
	DebugFlagPrint(mega, MEGA_ISR, ("racISR        : hifr=%#x, ioState=%d\n", hifr, pMDI->osInfo.ioState));

	if ((hifr & HIFR_ANY_INTR) == 0)
		return;		/* not our interrupt */

	/* disable interrupt to host (i.e. this driver),
	   otherwise we get a continuing deluge of interrupts */

	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DEFAULTS_NO_HOST);

	queue_task(&pMDI->osInfo.isrBHqueue, &tq_immediate);	/* schedule bottom-half */
	mark_bh(IMMEDIATE_BH);

}				/* end of racISR() */

/*****************************************************************************
 *
 ****************************************************************************/
static void
racIsrBottomHalf(void *data)
{
	MEGARAC_DEVICE_INFO *pMDI = (MEGARAC_DEVICE_INFO *) data;
	RAC_OS_INFO *pOSI = &pMDI->osInfo;

	/* if interrupts just got turned back on */
	if (racIsrDpc(&pMDI->racInfo)) {
		/* indicate a i/o has completed */
		pOSI->ioState = MR_IO_WRITE_COMPLETE;

		/* start any pending read() */
		/* The argument to this is of different types in different kernels */
		/* andrewm@ami.com 6/19/2001 */
		wake_up_interruptible(&pOSI->readQ);
	}

}				/* end of racIsrBottomHalf() */

/*********************************************************
 *
 *********************************************************/
static int
racRead(struct file *fp, char *pData, size_t dataLen, loff_t * ignored)
{

/* In 2.4.x series kernels, we also need to set the current thread back to */
/* the TASK_RUNNING state and remove ourself from the wait queue. */
/* andrewm@ami.com 6/19/2001 */
#define RAC_READ_EXIT(retv) {                 \
    	pOSI->ioState = MR_IO_IDLE;               \
    	set_current_state(TASK_RUNNING);            \
		remove_wait_queue( &pOSI->readQ, &wait ); \
		wake_up( &pOSI->writeQ );   \
	    return retv;                            }

	RAC_INFO *pRAC = fp->private_data;
	RAC_OS_INFO *pOSI = pRAC->pOsInfo;
	CCB_Header *pCCB = (CCB_Header *) pOSI->pContiguousBuffer;
	unsigned long length;

	DECLARE_WAITQUEUE(wait, current);

	DebugFlagPrint(mega, MEGA_DEVICE,
		       ("racRead       : fp=%p, pData=%p, dataLen=%#lx, loff=%p, ioState=%d\n",
			fp, pData, dataLen, ignored, pOSI->ioState));

	/* validate the request */

	if (pData == NULL || dataLen == 0) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racRead       : unknown data %p, %#x\n", pData, dataLen));
		return -ENOTTY;
	}

	if (pOSI->ioState == MR_IO_IDLE) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racRead       : no i/o outstanding\n"));
		return -ENOTTY;
	}

	/* Add the current context to the wait queue and set ourself as interruptible */
	/* andrewm@ami.com 6/19/2001 */
	add_wait_queue(&pOSI->readQ, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	while (pOSI->ioState != MR_IO_WRITE_COMPLETE) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racRead       : waiting\n"));

		if (signal_pending(current)) {
			/* Set ourself back to running mode and take ourself off the wait queue */
			/* andrewm@ami.com 6/19/2001 */
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&pOSI->readQ, &wait);
			return -ERESTARTSYS;
		}

		/* Go to sleep waiting for a wakeup */
		/* andrewm@ami.com 6/19/2001 */
		schedule();
	}
	remove_wait_queue(&pOSI->readQ, &wait);
	

	/* copy completed request back to user memory space */

	length = sizeof(CCB_Header) + pCCB->Length;

	set_current_state(TASK_RUNNING);

	if (length > dataLen || copy_to_user(pData, pCCB, length)) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racRead       : copy_to_user failed, length=%ld\n", length));
	    	pOSI->ioState = MR_IO_IDLE;
		wake_up( &pOSI->writeQ );
		return -EFAULT;
	}

	/* if any write()'s are waiting then start next i/o */
	
    	pOSI->ioState = MR_IO_IDLE;
	wake_up( &pOSI->writeQ );
	return length; /* success */
}				/* end of racRead() */

/*********************************************************
 *  
 *********************************************************/
static int
racIoctl(struct inode *ip, struct file *fp, unsigned int ioControlCode, unsigned long datap)
{
	RAC_INFO *pRAC = fp->private_data;
	RAC_OS_INFO *pOSI = pRAC->pOsInfo;
	MEGARAC_IO_BUFS miob;
	struct _ccbEvt {
		CCB_Header ccb;
		RAC_EVENT_NOTIFICATION events;
	} ccbEvt;

	/* Declare our own wait queue */
	/* andrewm@ami.com 6/19/2001 */
	DECLARE_WAITQUEUE(wait, current);

	DebugFlagPrint(mega, MEGA_DEVICE,
		       ("racIoctl      : ip=%p, fp=%p, ioCC=%#x, datap=%#lx, count=%d\n",
			ip, fp, ioControlCode, datap, pOSI->events.eventHandle[racEventFirmwareRequest]));

	if (ioControlCode != _IO(RAC_IOC_MAGIC, IOCTL_EVENT_WAIT) || datap == 0) {
		DebugFlagPrint(mega, MEGA_DEVICE,
			       ("racIoctl        unknown ioControlCode=%#x, datap=%p\n", ioControlCode, datap));
		return -ENOTTY;
	}

	/* construct control structure in kernelspace */

	copy_from_user_ret(&miob, (void *) datap, sizeof(miob), -EFAULT);

	DebugFlagPrint(mega, MEGA_DEVICE,
		       ("                ioCC=%#lx, reqBuf=%p, reqLen=%#lx, respBuf=%p, respLen=%#lx\n",
			miob.ioControlCode, miob.requestBuf, miob.requestBufLen,
			miob.responseBuf, miob.responseBufLen));

	/* construct ccb in kernelspace */

	if (miob.requestBufLen < sizeof(ccbEvt.ccb) || miob.responseBufLen < sizeof(ccbEvt.ccb))
		return -ENOTTY;

	copy_from_user_ret(&ccbEvt.ccb, miob.requestBuf, sizeof(ccbEvt.ccb), -EFAULT);

	switch (ccbEvt.ccb.Command) {

//        case MEGARAC_API_CMD_OS_RESTART:
//            DebugFlagPrint( mega, MEGA_DEVICE, ("racIoctl      : issuing reboot\n") );
//            {   
//                typedef int (*sysfun_p)();
//                extern long   sys_call_table[];
//                void         *kfunc = (void*)sys_call_table[__NR_reboot];
//                int           err   = ((sysfun_p)kfunc)( LINUX_REBOOT_MAGIC1, 
//                                                         LINUX_REBOOT_MAGIC2, 
//                                                         LINUX_REBOOT_CMD_HALT, NULL );
//                if ( err ) {
//                    DebugFlagPrint( mega, MEGA_DEVICE, ("racIoctl      : sys_reboot=%d\n",err) );
//                    return err;
//                }
//                ccbEvt.ccb.Status = MEGARAC_ERR_NONE;
//                copy_to_user( miob.responseBuf, &ccbEvt.ccb, sizeof(ccbEvt.ccb) );
//            }
//            break;

	case MEGARAC_API_CMD_WAIT_EVENTS:
		if (miob.responseBufLen != sizeof(ccbEvt) || ccbEvt.ccb.Length != sizeof(pOSI->events)) {
			DebugFlagPrint(mega, MEGA_DEVICE, ("racIoctl      : unknown length=%#x\n", ccbEvt.ccb.Length));
			return -ENOTTY;
		}

		/* Add the current context to the wait queue and set ourself as interruptible */
		/* andrewm@ami.com 6/19/2001 */
		add_wait_queue(&pOSI->ioctlQ, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		while (pOSI->events.eventHandle[racEventFirmwareRequest] == 0) {
			DebugFlagPrint(mega, MEGA_DEVICE, ("racIoctl      : waiting\n"));
			if (signal_pending(current)) {
				/* Set ourself back to running mode and take ourself off the wait queue */
				/* andrewm@ami.com 6/19/2001 */
				set_current_state(TASK_RUNNING);
				remove_wait_queue(&pOSI->ioctlQ, &wait);
				return -ERESTARTSYS;
			}

			/* Go to sleep waiting for a wakeup from read */
			/* andrewm@ami.com 6/19/2001 */
			schedule();
		}

		/* We don't need to wait any more, so set ourselves back to running, */
		/* and remove ourselves from the wait queue */
		/* andrewm@ami.com 6/19/2001 */
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&pOSI->ioctlQ, &wait);

		ccbEvt.ccb.Status = MEGARAC_ERR_NONE;
		memcpy(&ccbEvt.events, &pOSI->events, sizeof(ccbEvt.events));
		memset(&pOSI->events, 0, sizeof(pOSI->events));

		copy_to_user(miob.responseBuf, &ccbEvt, sizeof(ccbEvt));

		DebugFlagPrint(mega, MEGA_DEVICE,
			       ("racIoctl      : wait event exit: down=%d, cursor=%d, ftp=%d\n",
				ccbEvt.events.eventHandle[racEventRequestHostOsShutdown],
				ccbEvt.events.eventHandle[racEventCursorChange],
				ccbEvt.events.eventHandle[racEventPassThruData]));
		break;

	default:
		DebugFlagPrint(mega, MEGA_DEVICE, ("racIoctl      : unknown ccb.command=%#x\n", ccbEvt.ccb.Command));
		return -ENOTTY;
	}

	return 0;
}				/* end of racIoctl() */

/****************************************************
 * additional source files 
 ****************************************************/

#include "MegaRacDrvr.h"
#include "MegaRacDLL.h"

static void (*racOsEventProc) (RAC_INFO * pRAC, RAC_EVENT rawEvent, RAC_EVENT firmEvent);
static void (*racOsSetEventsProc) (RAC_INFO * pRAC, CCB_Header * pCCB);
static void (*racOsAttachmentsProc) (RAC_INFO * pRAC, CCB_Header * pCCB);
static unsigned short (*racGetHardwareCursor) (void);

/*****************************************************************************
 *    
 ****************************************************************************/
#ifdef DEBUG_PRINT
static void
racDumpRegs(RAC_INFO * pRAC, char *str)
{
	unsigned char himr, hifr, hasr, hfr, hcr;

	himr = READ_RAC_UCHAR(pRAC->, portAddrHIMR);
	hifr = READ_RAC_UCHAR(pRAC->, portAddrHIFR);
	hasr = READ_RAC_UCHAR(pRAC->, portAddrHASR);
	hfr = READ_RAC_UCHAR(pRAC->, portAddrHFR);
	hcr = READ_RAC_UCHAR(pRAC->, portAddrHCR);

	DebugPrintf(("MegaRac dump  : %s: "
		     "himr=%#x, hifr=%#x, hasr=%#x, hfr=%#x, hcr=%#x\n", str ? str : " ", himr, hifr, hasr, hfr, hcr));
}				/* end of racDumpRegs() */
#endif

/*****************************************************************************
 *    
 ****************************************************************************/
#ifdef DEBUG_PRINT
static char *
racIoctlToString(unsigned long ioControlCode)
{
	switch (ioControlCode) {
	case IOCTL_ISSUE_RCS:
		return "IOCTL_ISSUE_RCS";
	case IOCTL_GET_GRAPHICS:
		return "IOCTL_GET_GRAPHICS";
	case IOCTL_RESET_CARD:
		return "IOCTL_RESET_CARD";
	case IOCTL_API_INTERNAL:
		return "IOCTL_API_INTERNAL";
	}
	return "IOCTL_Unknown";
}				/* end of racIoctlToString() */
#endif

/*****************************************************************************
 *    
 ****************************************************************************/
#ifdef DEBUG_PRINT
static char *
racFriToString(int fri)
{
	switch (fri) {
	case GetDCInfo_FRI:
		return "GetDCInfo_FRI";
	case GetDCPacket_x_FRI:
		return "GetDCPacket_x_FRI";
	case SendDCComplete_FRI:
		return "SendDCComplete_FRI";
	case SendKeyEvent_FRI:
		return "SendKeyEvent_FRI";
	case SendMouseEvent_FRI:
		return "SendMouseEvent_FRI";
	case SendAlertToHost_FRI:
		return "SendAlertToHost_FRI";
	case HOST_OS_SHUTDOWN_REQUEST_FRI:
		return "HOST_OS_SHUTDOWN_REQUEST_FRI";
	case ModeChangedToGraphics_FRI:
		return "ModeChangedToGraphics_FRI";
	case ServiceDYRM_FRI:
		return "ServiceDYRM_FRI";
	case PassThruData_FRI:
		return "PassThruData_FRI";
	}
	return "FRI_Unknown";
}				/* end of racFriToString() */
#endif

/*****************************************************************************
 *    
 ****************************************************************************/
static void
racSetAddrs(RAC_INFO * pRAC, void *baseAddr)
{
	unsigned char *address = (unsigned char *) baseAddr;
	pRAC->portAddrBase = address;
	pRAC->portAddrHIMR = address + HOST_INTERRUPT_MASK_REG;
	pRAC->portAddrHIFR = address + HOST_INTERRUPT_FLAG_REG;
	pRAC->portAddrHCMDR = address + HOST_COMMAND_REG;
	pRAC->portAddrDATA = (unsigned long *) (address + HOST_DATA_OUT_REG);
	pRAC->portAddrHASR = address + HOST_ADAPTER_STATUS_REG;
	pRAC->portAddrHFR = address + HOST_FLAGS_REG;
	pRAC->portAddrHCR = address + HOST_CONTROL_REG;

	/* sanity check */

	if (pRAC->pOsInfo == NULL) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSetAddrs   : warning: pOsInfo (and maybe pRacInfo) not set\n"));
	}

	/* make compiler happy by always referencing some functions that are not always used */

	if (baseAddr == NULL || ((unsigned long) baseAddr & 0x01)) {
#ifdef DEBUG_PRINT
		racDumpRegs(pRAC, "racSetAddrs: SHOULD NEVER HAPPEN!!!!");
#endif
		racClearAddrs(pRAC);
	}
}				/* end of racSetAddrs() */

/*****************************************************************************
 *    
 ****************************************************************************/
static void
racClearAddrs(RAC_INFO * pRAC)
{
	pRAC->portAddrBase = NULL;
	pRAC->portAddrHIMR = NULL;
	pRAC->portAddrHIFR = NULL;
	pRAC->portAddrHCMDR = NULL;
	pRAC->portAddrDATA = NULL;
	pRAC->portAddrHASR = NULL;
	pRAC->portAddrHFR = NULL;
	pRAC->portAddrHCR = NULL;
}				/* end of racClearAddrs() */

/*****************************************************************************
 * on entry to this routine 
 *      pRAC->dmiHeader.Signature[0]==0
 * if the DMI information is found in the BIOS, then when this routine exits
 *      pRAC->dmiHeader.Signature[0]!=0
 * this zero or non-zero result is used elsewhere in the driver code
 *
 * note: not all bios's have dmi information,
 * to confirm, use dos debug command:     s 000f:0000 ffff "_DMI_"
 ****************************************************************************/
static void
racFindDmiInBios(RAC_INFO * pRAC)
#warning need to fix this to use the normal dmi driver
{
#define          BIOS_LENGTH (BIOS_STOP_ADDR - BIOS_START_ADDR)
	unsigned char *pBiosStart = NULL,	/* runtime validity checked below */
	*pBiosCur, *pBiosStop;
	DMI_HEADER *pDmiHdr;

	pBiosStart = phys_to_virt(BIOS_START_ADDR);

	if (pBiosStart == NULL)	/* no known driver interface */
		return;		/* ...was defined above */

	for (pBiosStop = pBiosStart + BIOS_LENGTH - sizeof(DMI_HEADER),
	     pBiosCur = pBiosStart; pBiosCur < pBiosStop; pBiosCur += sizeof(DMI_HEADER)) {

		pDmiHdr = (DMI_HEADER *) pBiosCur;	/* default assumption */

		if (pDmiHdr->Signature[0] == '_' &&
		    pDmiHdr->Signature[1] == 'D' &&
		    pDmiHdr->Signature[2] == 'M' && pDmiHdr->Signature[3] == 'I' && pDmiHdr->Signature[4] == '_') {
			memcpy(&pRAC->dmiHeader, pDmiHdr, sizeof(pRAC->dmiHeader));
			break;	/* found it */
		}
	}			/* end 'for pBiosCur' */

}				/* end of racFindDmiInBios() */

/*****************************************************************************
 *    undocumented features between DLL and Driver:
 *       1) Allow DLL to retrieve the DMI header without the
 *          DMI data, because the DLL doesn't know the size of the data.
 *       2) Allow the Driver to insert the DMI data into the
 *          DLL's CCB buffer, because only the Driver has the data.
 *    Besides the obvious ccb.Command value, 
 *    the magic indicator for these features is ccb.Length 
 ****************************************************************************/
static BOOL
racHandleReportDMI(RAC_INFO * pRAC, CCB_Header * pCCB)
{
#define DMI_RCS_HDR_LENGTH (  sizeof(  RCS_FN_GET_DMI_INFO_ARGS) - \
                                  sizeof(((RCS_FN_GET_DMI_INFO_ARGS*)0)->DMIData)  )
	static BOOL tryFindOnce = FALSE;
	RCS_FN_GET_DMI_INFO_ARGS *pCcbDmi = (RCS_FN_GET_DMI_INFO_ARGS *) (pCCB + 1);
	void *pDmiData;

	DebugFlagPrint(mega, MEGA_ENTRY,
		       ("rac--ReportDMI: cLen=%#x, HDR=%#x, sLen=%#x\n",
			pCCB->Length, DMI_RCS_HDR_LENGTH, pRAC->dmiHeader.StructLength));

	/* load DMI info from BIOS 
	   because MegaRac can't access this address on a Pentium-II */

	if (!tryFindOnce) {
		tryFindOnce = TRUE;	/* never do again */
		racFindDmiInBios(pRAC);
	}

	/* try to service the CCB request */

	if (pRAC->dmiHeader.Signature[0] == 0) {	/* DMI info not available */
		pCCB->Status = RCSERR_INFORMATION_NOT_AVAILABLE;
		pCCB->Length = 0;
		return TRUE;
	}

	if (pCCB->Length < DMI_RCS_HDR_LENGTH) {	/* ccb request too small */
		pCCB->Status = RCSERR_BAD_ARGUMENT;
		pCCB->Length = 0;
		return TRUE;
	}

	/* copy DMI header to callers CCB */

	pCcbDmi->NumStructs = pRAC->dmiHeader.NumberOfStructs;
	pCcbDmi->BCDRevision = pRAC->dmiHeader.BCDRevision;
	pCcbDmi->Reserved = 0;
	pCcbDmi->DMIDataLen = pRAC->dmiHeader.StructLength;

	/* check if caller only wanted the dmi header (not the actual data) */

	if (pCCB->Length == DMI_RCS_HDR_LENGTH) {	/* DLL is requesting exactly */
		pCCB->Status = RCSERR_SUCCESS;	/* ...the DMI header length */
		return TRUE;	/* pass DMI header back to DLL */
	}

	/* caller wants the dmi data, will it fit? */

	if (pCCB->Length < DMI_RCS_HDR_LENGTH + pRAC->dmiHeader.StructLength) {
		pCCB->Status = RCSERR_BAD_ARGUMENT;	/* not enough space available */
		pCCB->Length = 0;	/* ...to copy the DMI data */
		return TRUE;
	}

	/* copy DMI data to callers CCB */

	pDmiData = NULL;	/* assume failure of #define's */

	{
		unsigned long physAddr;
		memcpy(&physAddr, &pRAC->dmiHeader.StructAddr, sizeof(physAddr));
		pDmiData = phys_to_virt(physAddr);
		if (pDmiData) {
			memcpy(pCcbDmi->DMIData, pDmiData, pRAC->dmiHeader.StructLength);
		}
	}

	if (pDmiData == NULL) {
		pCCB->Status = RCSERR_INFORMATION_NOT_AVAILABLE;
		pCCB->Length = 0;
		return TRUE;
	}

	/* the DMI was successfully moved to the callers CCB */

	pCCB->Length = pRAC->dmiHeader.StructLength + DMI_RCS_HDR_LENGTH;
	return FALSE;
}				/* end of racHandleReportDMI() */

/*****************************************************************************
 *  after a command is sent to the MegaRac, 
 *    check if the command effects any internal driver flags
 *  
 * if RCS_ADMIN_SHUTDOWN_CARD, then will be last cmd executed until host power cycle
 ****************************************************************************/
static void
racSendCcbMagic(RAC_INFO * pRAC, CCB_Header * pCCB, unsigned long ioControlCode)
{
	if (ioControlCode != IOCTL_ISSUE_RCS)
		return;

	switch (pCCB->Command) {
	case RCS_CMD_START_HEARTBEAT:
		pRAC->heartbeatStarted = TRUE;
		break;
	case RCS_CMD_STOP_HEARTBEAT:
		pRAC->heartbeatStarted = FALSE;
		break;
	case RCS_ADMIN_SHUTDOWN_CARD:
		pRAC->shutDownIssued = TRUE;
		pRAC->okToProcessRCS = FALSE;
		break;
	case RCS_ADMIN_RESET_CARD:
		pRAC->boardAliveCount = BOARD_ALIVE_RESET;
		pRAC->resetInProgress = TRUE;
		pRAC->okToProcessRCS = pRAC->specialModeInProgress = FALSE;
		break;
	}
}				/* end of racSendCcbMagic() */

/*****************************************************************************
 *  returns:    TRUE  - ok to issue ccb to the MegaRac
 *              FALSE - don't issue ccb to the MegaRac
 ****************************************************************************/
static BOOL
racSendCcbPre(RAC_INFO * pRAC, CCB_Header * pCCB, unsigned long ioControlCode, void **pyCCB)
{
	CCB_Header dummyCCB;
	void *yCCB;
	unsigned char hasr, hfr;

	if (pCCB == NULL)
		pCCB = &dummyCCB;

	/* sanity checks that should never fail */

	if (ioControlCode != IOCTL_ISSUE_RCS) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbPre : unknown IoControlCode=%#x\n", ioControlCode));
		pCCB->Status = RCSERR_BAD_ARGUMENT;
		pCCB->Length = 0;
		return FALSE;
	}

	if (pRAC->specialModeInProgress) {	/* use racSendCcbSpecialMode() instead */
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbPre : failed: specialModeInProgress\n"));
		pCCB->Status = RCSERR_FUNCTION_NOT_SUPPORTED;
		pCCB->Length = 0;
		return FALSE;
	}

	/* if a shutdown has previously been issued, or a reset is in process, 
	   then the board will never respond, so reject the command */

	if (pRAC->shutDownIssued) {	/* remains true until power reset */
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbPre : failed: shutDownIssued\n"));
		pCCB->Status = RCSERR_OS_SHUTDOWN_PENDING;
		pCCB->Length = 0;
		return FALSE;
	}

	if (pRAC->resetInProgress) {	/* cleared in racTimerTick() */
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbPre : failed: resetInProgress\n"));
		pCCB->Status = RCSERR_RESOURCES_OCCUPIED_RETRY;
		pCCB->Length = 0;
		return FALSE;
	}

	/* i/o registers are not properly setup */

	if (pRAC->portAddrBase == NULL) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbPre : failed: portAddrBase==NULL\n"));
		pCCB->Status = DRACERR_NULL_POINTER_ARGUMENT;
		pCCB->Length = 0;
		return FALSE;
	}

	/* only check hardware status if we actually have a ccb to send to the card */

	if (pyCCB) {

		/* is the megaRAC hardware alive */

		hasr = READ_RAC_UCHAR(pRAC->, portAddrHASR);
		hfr = READ_RAC_UCHAR(pRAC->, portAddrHFR);

		if ((hasr & HASR_FIRM_READY) == 0 || (hfr & HIFR_HACC)) {
			DebugFlagPrint(mega, MEGA_DEVICE,
				       ("racSendCcbPre : failed: MegaRAC not ready, hfr=%#x, hasr=%#x\n", hfr, hasr));
			pCCB->Status = RCSERR_RESOURCES_OCCUPIED_RETRY;
			pCCB->Length = 0;
			return FALSE;
		}

		if (hasr & HASR_FIRM_FAILED) {
			DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbPre : failed: MegaRAC dead, hasr=%#x\n", hasr));
			pCCB->Status = MEGARAC_ERR_DEAD;
			pCCB->Length = 0;
			return FALSE;
		}

		/* common place to convert address */

		VIRTUAL_TO_PHYSICAL(pRAC, yCCB, pCCB);
		if (yCCB)
			*pyCCB = yCCB;	/* successfully converted to a physical address */
		else {
			DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbPre : failed: yCCB==NULL\n"));
			pCCB->Status = DRACERR_NULL_POINTER_ARGUMENT;
			pCCB->Length = 0;
			return FALSE;
		}		/* end of 'if/else yCCB' */
	}
	/* end of 'if     pyCCB' */
	return TRUE;
}				/* end of racSendCcbPre() */

/*****************************************************************************
 *  returns:    TRUE  - ccb has been issued to the MegaRac
 *              FALSE - unable to issue ccb to the MegaRac
 ****************************************************************************/
static BOOL
racSendCcb(RAC_INFO * pRAC, CCB_Header * pCCB, unsigned long ioControlCode)
{
	void *yCCB;

	if (pCCB == NULL || !racSendCcbPre(pRAC, pCCB, ioControlCode, &yCCB))
		return FALSE;

	pRAC->boardAliveCount = BOARD_ALIVE_RESET;	/* give board a chance to process */

	/* give request to the MegaRAC */

	pRAC->currentCCB = pCCB;

	WRITE_RAC_ULONG(pRAC->, portAddrDATA, (unsigned long) yCCB);
	WRITE_RAC_UCHAR(pRAC->, portAddrHCMDR, HCMDR_ISSUE_RCS_CMD);

	racSendCcbMagic(pRAC, pCCB, ioControlCode);	/* update internal driver flags */

	return TRUE;
}				/* end of racSendCcb() */

/*****************************************************************************
 *  returns:    TRUE  - ccb has been issued to the MegaRac
 *              FALSE - unable to issue ccb to the MegaRac
 ****************************************************************************/
static BOOL
racSendCcbWait(RAC_INFO * pRAC, CCB_Header * pCCB, unsigned long ioControlCode)
{
#ifdef DEBUG_PRINT
#define     RAC_DUMP_REGS(msg)	/*racDumpRegs( pRAC, msg ) */
#else
#define     RAC_DUMP_REGS(msg)	/* always blank when debugging is disabled */
#endif

#define         ARBITRARY_WAIT_LIMIT  2000	/* note: ipmi requests take over one second */

	int limit_A, limit_B = 1, limit_C = 1;	/* prevent error message display if limit_A reached */
	unsigned char himr, hfr, hasr;
	BOOL retCode = TRUE;
	void *yCCB;

	if (pCCB == NULL)
		return FALSE;

	RAC_DUMP_REGS("before all      ");

	himr = READ_RAC_UCHAR(pRAC->, portAddrHIMR);	/* save current interrupts */
	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DISABLE_ALL);	/* disable all MegaRac interrupts */
	WRITE_RAC_UCHAR(pRAC->, portAddrHFR, HIFR_HACC);	/* clear HACC bit */

	/* wait for firmware to be ready to receive a new command */

	for (limit_A = ARBITRARY_WAIT_LIMIT; --limit_A;) {

		/* is 'firmware ready to receive command' */

		hasr = READ_RAC_UCHAR(pRAC->, portAddrHASR);
		if ((hasr & HASR_FIRM_READY) == 0) {
			mdelay(1);	/* not ready, wait a milliSec and try again */
			continue;
		}

		/* now the megarac is ready for the command */

		if (!racSendCcbPre(pRAC, pCCB, ioControlCode, &yCCB))
			return FALSE;

		RAC_DUMP_REGS("before write_rac");

		WRITE_RAC_ULONG(pRAC->, portAddrDATA, (unsigned long) yCCB);
		WRITE_RAC_UCHAR(pRAC->, portAddrHCMDR, HCMDR_ISSUE_RCS_CMD);

		RAC_DUMP_REGS("after  write_rac");

		/* wait for firmware to read the ccb address */

		for (limit_B = ARBITRARY_WAIT_LIMIT; --limit_B;) {
			hfr = READ_RAC_UCHAR(pRAC->, portAddrHFR);
			if (hfr & HIFR_DATA_OUT)	/* is 'Host Data Out Port Empty' */
				break;	/* yes, firmware has read the request */
			mdelay(1);	/* no, wait a milliSec and then try again */
		}
		RAC_DUMP_REGS("after do loop 1 ");

		/* wait for firmware to process the ccb */

		for (limit_C = ARBITRARY_WAIT_LIMIT; --limit_C;) {
			hfr = READ_RAC_UCHAR(pRAC->, portAddrHFR);
			if (hfr & HIFR_HACC)	/* is 'Host Adapter Cmd Complete' */
				break;	/* yes, firmware has serviced the request */
			mdelay(1);	/* no, wait a milliSec and then try again */
		}
		RAC_DUMP_REGS("after do loop 2 ");

		WRITE_RAC_UCHAR(pRAC->, portAddrHFR, HIFR_HACC);	/* clear HACC bit */
		break;
	}			/* end 'for limit_A' */

#ifdef DEBUG_PRINT
	if (limit_A <= 0)
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbWait: limit_A EXCEEDED!!!!\n"));
	if (limit_B <= 0)
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbWait: limit_B EXCEEDED!!!!\n"));
	if (limit_C <= 0)
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCcbWait: limit_C EXCEEDED!!!!\n"));
#endif

	if (limit_A <= 0 || limit_B <= 0 || limit_C <= 0) {
		pCCB->Status = RCSERR_RESOURCES_OCCUPIED_RETRY;
		pCCB->Length = 0;
		retCode = FALSE;
	}

	racSendCcbMagic(pRAC, pCCB, ioControlCode);	/* update internal driver flags */
	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, himr);	/* restore original interrupts */
	RAC_DUMP_REGS("at exit         ");
	return retCode;
}				/* end of racSendCcbWait() */

/*****************************************************************************
 *  returns:    TRUE  - command has been issued to the MegaRac
 *              FALSE - unable to issue command to the MegaRac
 ****************************************************************************/
static BOOL
racSendCommandWait(RAC_INFO * pRAC, unsigned char hcmdr)
{
#undef          ARBITRARY_WAIT_LIMIT
#define         ARBITRARY_WAIT_LIMIT 4000
	BOOL retStatus = TRUE;
	unsigned char himr;
	int limit_A;

	/* wait for firmware to be ready for this command */

	for (limit_A = ARBITRARY_WAIT_LIMIT; --limit_A;) {
		unsigned char hasr = READ_RAC_UCHAR(pRAC->, portAddrHASR);
		if ((hasr & HASR_FIRM_READY) == HASR_FIRM_READY)
			break;	/* firmware is ready */
		mdelay(1);	/* not ready, wait a milliSec and try again */
	}
	if (limit_A <= 0) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCommandWait: HASR_FIRM_READY EXCEEDED!!!!\n"));
		return FALSE;
	}

	/* now the megarac is ready for the command */

	himr = READ_RAC_UCHAR(pRAC->, portAddrHIMR);	/* save current interrupts */
	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DISABLE_ALL);	/* disable all MegaRac interrupts */
	WRITE_RAC_UCHAR(pRAC->, portAddrHCMDR, hcmdr);	/* issue the command */

	/* wait for firmware to consume this command */

	for (limit_A = ARBITRARY_WAIT_LIMIT; --limit_A;) {
		unsigned char hfr = READ_RAC_UCHAR(pRAC->, portAddrHFR);
		if (hfr & HIFR_HACC) {	/* is 'Host Adapter Command Complete' */
			break;	/* yes, firmware has serviced the request */
		}
		mdelay(1);	/* no, wait a milliSec and then try again */
	}

	if (limit_A <= 0) {
		DebugFlagPrint(mega, MEGA_DEVICE, ("racSendCommandWait: HIFR_HACC EXCEEDED!!!!\n"));
		retStatus = FALSE;
	}

	WRITE_RAC_UCHAR(pRAC->, portAddrHFR, HIFR_HACC);	/* clear HACC bit */
	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, himr);	/* restore original interrupts */
	return retStatus;
}				/* end of racSendCommandWait() */

/*****************************************************************************
 *    send MegaRac its I/O base address from the pciConfig info
 *     to solve some firmware problem with the PentiumII
 ****************************************************************************/
static void
racSendIoBaseAddr(RAC_INFO * pRAC)
{
	RCS_FN_REPORT_IO_BASE_ADDRESS_COMMAND *pCCB;

	MALLOC_CONTIGUOUS(pRAC->, pCCB, RCS_FN_REPORT_IO_BASE_ADDRESS_COMMAND *, sizeof(*pCCB));
	if (pCCB == NULL)
		return;

	memset(pCCB, 0, sizeof(*pCCB));
	pCCB->RCSCmdPkt.Cmd.FullCmd = RCS_CMD_REPORT_IO_BASE_ADDRESS;
	pCCB->RCSCmdPkt.Length = sizeof(RCS_FN_REPORT_IO_BASE_ADDRESS_ARGS);
	pCCB->Data.IoBaseAddress = (unsigned long) pRAC->portAddrBase;

	racSendCcbWait(pRAC, (CCB_Header *) pCCB, IOCTL_ISSUE_RCS);
	FREE_CONTIGUOUS(pRAC->, pCCB);
}				/* end of racSendIoBaseAddr() */

/*****************************************************************************
 * undocumented requirement:
 *   the host or driver must send a start heartbeat so the firmware knows that the
 *   host OS is available to respond to 'are you there' requests from the remote.
 *   Otherwise, the firmware will never issue FirmwareRequestInterrupts to the driver. 
 ****************************************************************************/
static void
racSendStartHeartBeat(RAC_INFO * pRAC)
{
	RCS_FN_START_HEARTBEAT_COMMAND *pCCB;

	MALLOC_CONTIGUOUS(pRAC->, pCCB, RCS_FN_START_HEARTBEAT_COMMAND *, sizeof(*pCCB));
	if (pCCB == NULL)
		return;

	memset(pCCB, 0, sizeof(*pCCB));
	pCCB->RCSCmdPkt.Cmd.FullCmd = RCS_CMD_START_HEARTBEAT;
	pCCB->RCSCmdPkt.Length = sizeof(RCS_FN_START_HEARTBEAT_ARGS);

	racSendCcbWait(pRAC, (CCB_Header *) pCCB, IOCTL_ISSUE_RCS);
	FREE_CONTIGUOUS(pRAC->, pCCB);
}				/* end of racSendStartHeartBeat() */

/*****************************************************************************
 *    MegaRac needs a stop heartbeat to know the driver is finished
 ****************************************************************************/
static void
racSendStopHeartBeat(RAC_INFO * pRAC)
{
	RCS_FN_STOP_HEARTBEAT_COMMAND *pCCB;

	MALLOC_CONTIGUOUS(pRAC->, pCCB, RCS_FN_STOP_HEARTBEAT_COMMAND *, sizeof(*pCCB));
	if (pCCB == NULL)
		return;

	memset(pCCB, 0, sizeof(*pCCB));
	pCCB->RCSCmdPkt.Cmd.FullCmd = RCS_CMD_STOP_HEARTBEAT;

	racSendCcbWait(pRAC, (CCB_Header *) pCCB, IOCTL_ISSUE_RCS);
	FREE_CONTIGUOUS(pRAC->, pCCB);
}				/* end of racSendStopHeartBeat() */

/*****************************************************************************
 *    
 ****************************************************************************/
static void
racSendHardReset(RAC_INFO * pRAC)
{
	CCB_Header dummyCCB;

	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DISABLE_ALL);	/* disable all MegaRac interrupts */
	WRITE_RAC_UCHAR(pRAC->, portAddrHCR, HCR_HARD_RESET);	/* ...to prevent spurious */

	dummyCCB.Command = RCS_ADMIN_RESET_CARD;	/* soft reset is ok for this magic */
	racSendCcbMagic(pRAC, &dummyCCB, IOCTL_ISSUE_RCS);
}				/* end of racSendHardReset() */

/*****************************************************************************
 *    
 ****************************************************************************/
static BOOL
racGetRegister(RAC_INFO * pRAC, CCB_Header * pCCB)
{
	BOOL status = TRUE;
	unsigned long value;

	switch (pCCB->Reserved[0]) {
	case HOST_INTERRUPT_MASK_REG:
		value = (unsigned long) READ_RAC_UCHAR(pRAC->, portAddrHIMR);
		break;
	case HOST_INTERRUPT_FLAG_REG:
		value = (unsigned long) READ_RAC_UCHAR(pRAC->, portAddrHIFR);
		break;
	case HOST_COMMAND_REG:
		value = (unsigned long) READ_RAC_UCHAR(pRAC->, portAddrHCMDR);
		break;
	case HOST_DATA_IN_REG:
		value = READ_RAC_ULONG(pRAC->, portAddrDATA);
		break;
	case HOST_ADAPTER_STATUS_REG:
		value = (unsigned long) READ_RAC_UCHAR(pRAC->, portAddrHASR);
		break;
	case HOST_FLAGS_REG:
		value = (unsigned long) READ_RAC_UCHAR(pRAC->, portAddrHFR);
		break;
	case HOST_CONTROL_REG:
		value = (unsigned long) READ_RAC_UCHAR(pRAC->, portAddrHCR);
		break;
	default:
		value = 0;
		status = FALSE;
		break;
	}

	pCCB->Reserved[2] = (unsigned char) ((value >> 24) & 0x00ff);
	pCCB->Reserved[3] = (unsigned char) ((value >> 16) & 0x00ff);
	pCCB->Reserved[4] = (unsigned char) ((value >> 8) & 0x00ff);
	pCCB->Reserved[5] = (unsigned char) ((value) & 0x00ff);

	return status;
}				/* end of racGetRegister() */

/*****************************************************************************
 *    -----NEVER USED and NEVER TESTED------- 
 ****************************************************************************/
#if 0
static void racSendEnterSpecialMode(RAC_INFO * pRAC, unsigned char specialMode);
static void
racSendEnterSpecialMode(RAC_INFO * pRAC, unsigned char specialMode)
{
#define HCMDR_ENTER_IMAGE_FLASH_MODE 0x40	/* enter image flash mode */
	int i;

	/* a double special situation, 
	   request is to flash the SDK block, rather than the firmware image */

	if (specialMode == HCMDR_ENTER_SDK_FLASH_MODE) {
		racSendCommandWait(pRAC, specialMode);
		specialMode = HCMDR_ENTER_IMAGE_FLASH_MODE;	/* a white lie for next phase */
	}

	/* special modes can only be entered during a small window of time after a reset */

	racSendHardReset(pRAC);

	for (i = 0; i < (10 * 1000); i++) {	/* wait maximum of ten seconds */
		unsigned char hfr = READ_RAC_UCHAR(pRAC->, portAddrHFR);
		if (hfr & HIFR_SOFT_INT) {	/* megarac is coming alive */
			WRITE_RAC_UCHAR(pRAC->, portAddrHFR, HIFR_SOFT_INT);	/* reset the bit */
			WRITE_RAC_UCHAR(pRAC->, portAddrHCMDR, specialMode);	/* tell megarac the desired mode */
			pRAC->specialModeInProgress = TRUE;
			mdelay(1);	/* wait a milliSec for mode to invoke */
			return;	/* success */
		}
		mdelay(1);	/* wait a milliSec and then try again */
	}

	/* failed to capture the reset */

	racSendHardReset(pRAC);	/* kick megarac back to normal mode */
}				/* end of racSendEnterSpecialMode() */
#endif

/*****************************************************************************
 *    
 ****************************************************************************/
static void
racStartupFinal(RAC_INFO * pRAC)
{
	unsigned char hasr = READ_RAC_UCHAR(pRAC->, portAddrHASR);

	DebugFlagPrint(mega, MEGA_ENTRY, ("racStartupFina: hasr=%#x\n", hasr));

	pRAC->okToProcessRCS = FALSE;	/* racPreProcessCCB will reject future commands */

	/* is 'firmware ready to receive command' */

	if ((hasr & HASR_FIRM_READY) == 0 || (hasr & HASR_FIRM_FAILED)) {	/* bad news, board is probably very dead */
		racSendHardReset(pRAC);	/* make attempt to kick-start */
		DebugFlagPrint(mega, MEGA_ENTRY, ("racStartupFina: card is dead, trying to reset\n"));
		return;
	}

	/* send MegaRac its I/O base address from the pciConfig info */

	racSendIoBaseAddr(pRAC);

	/* let MegaRac know that the driver is now running */

	racSendStartHeartBeat(pRAC);

	/* enable MegaRAC interrupts */

	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DISABLE_ALL);	/* disable all MegaRac interrupts */
	WRITE_RAC_UCHAR(pRAC->, portAddrHCR, HCR_INTR_RESET);	/* reset all interrupt bits */
	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DEFAULTS);	/* enable desired interrupts */

	/* the driver is now ready to service host requests */

	pRAC->okToProcessRCS = TRUE;

}				/* end of racStartupFinal() */

/*****************************************************************************
 *    
 ****************************************************************************/
static void
racShutdownBegin(RAC_INFO * pRAC)
{
	/* the driver can no longer service host requests */

	pRAC->okToProcessRCS = FALSE;

	/* if an error occurs during driver initialization this function
	   might get called before all the i/o registers are setup */

	if (pRAC->portAddrBase == NULL)
		return;

	/* megarac needs a stop heartbeat to know the driver is finished */

	if (pRAC->heartbeatStarted)
		racSendStopHeartBeat(pRAC);

	/* disable any future interrupts from the MegaRac */

	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DISABLE_ALL);
	WRITE_RAC_UCHAR(pRAC->, portAddrHCR, HCR_INTR_RESET);

	/* if this is an operating system that is exiting to DOS,
	   then need to leave HACC bit set to allow DOS based programs to work.
	   Specifically this would be NetWare and raccfg */

	WRITE_RAC_UCHAR(pRAC->, portAddrHCMDR, HCMDR_SET_HACC);

}				/* end of racShutdownBegin() */

/*****************************************************************************
 *
 ****************************************************************************/
static void
racPreProcessCCB(RAC_INFO * pRAC,
		 CCB_Header * pCCB, unsigned long bufLen, unsigned long ioControlCode, RAC_DRVR_ERR * drvrStatus)
{

	if (!pCCB) {		/* prevent occasional surprise */
		*drvrStatus = racDrvrErrNotImplemented;
		return;
	}

	DebugFlagPrint(mega, MEGA_STARTIO,
		       ("racPreProcessC: ccb.cmd=%#x, ccb.status=%#x, ccb.length=%#x\n",
			pCCB->Command, pCCB->Status, pCCB->Length));

	*drvrStatus = racDrvrErrNone;	/* hope for the best */

	switch (ioControlCode) {
	case IOCTL_ISSUE_RCS:
		if (!racSendCcbPre(pRAC, pCCB, IOCTL_ISSUE_RCS, NULL))
			break;	/* unable to process, return ccb to api */

		if (!pRAC->okToProcessRCS) {
			pCCB->Status = MEGARAC_ERR_FAILED;
			pCCB->Length = 0;
			break;	/* unable to process, return ccb to api */
		}

		if (pCCB->Command == RCS_CMD_REPORT_DMI_INFO) {
			if (racHandleReportDMI(pRAC, pCCB))
				break;	/* request has been fulfilled, return ccb to api */
		} /* else fall thru and send ccb to megarac */
		else if (pCCB->Command == RCS_RED_REPORT_CURSOR_POS && racGetHardwareCursor) {
			REPORT_CURSOR_POS_PKT *pRCP = (REPORT_CURSOR_POS_PKT *) (pCCB + 1);
			pRCP->LinearCursorPos = (*racGetHardwareCursor) ();
			DebugFlagPrint(mega, MEGA_STARTIO,
				       ("              : stuffed cursor=%#x\n", pRCP->LinearCursorPos));
		}
		/* fall thru and send ccb to megarac */
#ifdef DEBUG_PRINT
		if (DebugFlagIsSet(mega, MEGA_POLL_MODE)) {
			BOOL bStatus = racSendCcbWait(pRAC, pCCB, IOCTL_ISSUE_RCS);
			if (!bStatus) {
				pCCB->Status = RCSERR_DEVICE_NO_RESPONSE;
				pCCB->Length = 0;
			}
			DebugPrintf(("racPreProcessC: in MEGA_POLL_MODE status=%s ------------\n",
				     bStatus ? "good" : "bad"));
			break;
		}
#endif

		*drvrStatus = racDrvrErrPending;	/* processing not complete */
		break;		/* ... still need to send to megarac */

/* according to Jose 07/12/99, this is not used, but need to test to be sure */
	case IOCTL_GET_GRAPHICS:
		if (bufLen < sizeof(pRAC->firmwareRequestDCPacket))
			*drvrStatus = racDrvrErrInvalidParameter;
		break;		/* OS driver must copy data into callers buffer */

	case IOCTL_RESET_CARD:
		pCCB->Status = RCSERR_SUCCESS;
		pCCB->Length = 0;
		racSendHardReset(pRAC);
		break;

	case IOCTL_API_INTERNAL:
		pCCB->Status = RCSERR_SUCCESS;	/* be optimistic */
		pCCB->Length = 0;
		/* specialMode never used, and never tested
		   //if ( xxx ) {          * a special mode is being requested *
		   //    racSendEnterSpecialMode( pRAC, pCCB->Reserved[0] );
		   //    if ( !pRAC->specialModeInProgress )
		   //        pCCB->Status = RCSERR_DEVICE_NO_RESPONSE;
		   //}
		   //else */
		switch (pCCB->Command) {
		case MEGARAC_API_CMD_ATTACHMENTS:
			if (racOsAttachmentsProc)
				(*racOsAttachmentsProc) (pRAC, pCCB);
			else
				pCCB->Status = RCSERR_FUNCTION_NOT_SUPPORTED;
			break;
		case MEGARAC_API_CMD_ISSUE_CMD:
			if (!racSendCommandWait(pRAC, pCCB->Reserved[0]))
				pCCB->Status = RCSERR_DEVICE_NO_RESPONSE;
			break;
		case MEGARAC_API_CMD_GET_REG:
			if (!racGetRegister(pRAC, pCCB))
				pCCB->Status = RCSERR_FUNCTION_NOT_SUPPORTED;
			break;
		case MEGARAC_API_CMD_SET_EVENTS:
			if (racOsSetEventsProc)
				(*racOsSetEventsProc) (pRAC, pCCB);
			else
				pCCB->Status = RCSERR_FUNCTION_NOT_SUPPORTED;
			break;
		default:
			pCCB->Status = RCSERR_FUNCTION_NOT_SUPPORTED;
			break;
		}
		break;

	default:
		DebugFlagPrint(mega, MEGA_STARTIO, ("racPreProcessC: unknown IoControlCode=%#x\n", ioControlCode));
		*drvrStatus = racDrvrErrNotImplemented;
		break;
	}			/* end of 'switch ioControlCode' */

	DebugFlagPrint(mega, MEGA_STARTIO,
		       ("              : exit drvrStatus=%#x, ioctl=%s\n",
			*drvrStatus, racIoctlToString(ioControlCode)));
}				/* end of racPreProcessCCB() */

/*****************************************************************************
 *  bottom-half of ISR
 ****************************************************************************/
static BOOL
racIsrDpc(RAC_INFO * pRAC)
{
	BOOL didIoComplete = FALSE;
	unsigned char hfr, hasr, himr;
	unsigned long data;

	/* get all possible info from board before reseting interrupt bits */

	hasr = READ_RAC_UCHAR(pRAC->, portAddrHASR);
	hfr = READ_RAC_UCHAR(pRAC->, portAddrHFR);
	data = READ_RAC_ULONG(pRAC->, portAddrDATA);

	DebugFlagPrint(mega, MEGA_DPCISR,
		       ("racIsrDpc     : hfr=%#x, hasr=%#x, dataIn=%#x, curCCB=%#x\n",
			hfr, hasr, data, pRAC->currentCCB));

	/* reset interrupt bits, any new events will cause a new interrupt */

	WRITE_RAC_UCHAR(pRAC->, portAddrHFR, hfr);

	/* board is alive because it gave us this interrupt */

	pRAC->boardAliveCount = BOARD_ALIVE_RESET;

	/* check if new firmware request from MegaRAC */

	if (hfr & HIFR_FIRM_REQ) {
		pRAC->firmwareRequestData = data;
		racEvent(pRAC, racEventFirmwareRequest);
	}

	/* check if board has completed a host command */

	himr = HIMR_DEFAULTS;

	if (hfr & HIFR_HACC) {
		CCB_Header *pCCB = pRAC->currentCCB;

		if (pRAC->resetInProgress)
			himr = HIMR_DISABLE_ALL;	/* disable all MegaRac interrupts */

		if (pCCB == NULL) {
			DebugFlagPrint(mega, MEGA_DPCISR, ("************* : no currentCCB\n"));
		} else {
			DebugFlagPrint(mega, MEGA_DPCISR,
				       ("                ccb.cmd=%#x, ccb.status=%#x, ccb.length=%#x\n",
					pCCB->Command, pCCB->Status, pCCB->Length));
			pRAC->currentCCB = NULL;
			didIoComplete = TRUE;
		}
	}

	WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, himr);	/* enable desired interrupts */

	return didIoComplete;
}				/* end of racIsrDpc() */

/*****************************************************************************
 *  one second has elapsed
 ****************************************************************************/
static void
racTimerTick(RAC_INFO * pRAC)
{
	static unsigned short lastCursorPosition = ~0;

	static BOOL oncePerFailure = FALSE;
	unsigned char hasr = READ_RAC_UCHAR(pRAC->, portAddrHASR);

#if 0
	DebugFlagPrint(mega, MEGA_TIMEOUT,
		       ("racTimerTick   : hasr=%#x, resetInProgress=%#x\n", hasr, pRAC->resetInProgress));
#endif

	/* if a soft or hard reset was issued,
	   we hope the board will eventually recover successfully 
	   which will be indicated by HASR_FIRM_READY being set.
	   If the board does not recover, then eventually 
	   racInfo.boardAliveCount will expire and we'll signal the host */

	if (pRAC->resetInProgress) {	/* reset was recently issued */
		DebugFlagPrint(mega, MEGA_TIMEOUT,
			       ("racTimerTick  : resetInProgress=%#x, hasr=%#x\n", pRAC->resetInProgress, hasr));
		if (pRAC->specialModeInProgress)
			pRAC->boardAliveCount = BOARD_ALIVE_RESET;
		else if (hasr & HASR_FIRM_READY) {	/* has board recovered yet */
			pRAC->resetInProgress = FALSE;	/* yes */
			if (pRAC->portAddrBase) {	/* racStartupFinal() probably ran ok */
				pRAC->okToProcessRCS = TRUE;
				pRAC->boardAliveCount = BOARD_ALIVE_RESET;
				WRITE_RAC_UCHAR(pRAC->, portAddrHCR, HCR_INTR_RESET);	/* reset all interrupt bits */
				WRITE_RAC_UCHAR(pRAC->, portAddrHIMR, HIMR_DEFAULTS);	/* enable desired interrupts */
			}
		}
	}

	/* is firmware+hardware happy */

	if ((hasr & HASR_FIRM_FAILED) == 0) {
		oncePerFailure = FALSE;	/* everything ok */
	} else {		/* board is dead */
		if (!oncePerFailure) {	/* send alert, if haven't done yet */
			DebugFlagPrint(mega, MEGA_TIMEOUT,
				       ("racTimerTick  : firmware failure\n"
					"************* : hasr=%#x, data=%#x, resetInProgress=%#x\n",
					hasr, READ_RAC_ULONG(pRAC->, portAddrDATA), pRAC->resetInProgress));
			oncePerFailure = TRUE;	/* don't send again until board is alive once */
			racEvent(pRAC, racEventBoardDead);
		}
	}

	/* no response from board, get back any outstanding i/o */

	if (++pRAC->boardAliveCount >= RAC_CARD_READY_TIMEOUT) {
		pRAC->boardAliveCount = BOARD_ALIVE_RESET;
		if (pRAC->currentCCB) {
			DebugFlagPrint(mega, MEGA_TIMEOUT,
				       ("racTimerTick  : boardAliveCount failure, hasr=%#x\n", hasr));
			racEvent(pRAC, racEventBoardDead);
		}
	}

	/* on some platforms cursor position updates is done with software to work around asic bug */

	if (racGetHardwareCursor &&	/* driver provides software cursor function */
	    racOsEventProc &&	/* ...and has a way to signal application program */
	    !pRAC->resetInProgress &&	/* soft or hard reset       not      issued */
	    !pRAC->shutDownIssued) {	/* RCS_ADMIN_SHUTDOWN_CARD  not      issued */
		unsigned short currentCursorPosition = (*racGetHardwareCursor) ();
		if (lastCursorPosition != currentCursorPosition) {	/* the cursor has moved */
			DebugFlagPrint(mega, MEGA_TIMEOUT,
				       ("racTimerTick  : lastCursor=%#x, currentCursor=%#x\n", lastCursorPosition,
					currentCursorPosition));
			lastCursorPosition = currentCursorPosition;
			(*racOsEventProc) (pRAC, racEventFirmwareRequest, racEventCursorChange);
		}
	}

	/* send heartbeat to megarac to tell firmware that driver is alive */

#if 0
	DebugFlagPrint(mega, MEGA_TIMEOUT,
		       ("racTimerTick  : heartbeatStarted=%#x, resetInProgress=%#x, shutDownIssued=%#x\n"
			"                hasr=%#x, heartbeatCount=%#x\n",
			pRAC->heartbeatStarted, pRAC->resetInProgress, pRAC->shutDownIssued,
			hasr, pRAC->heartbeatCount));
#endif

	if (pRAC->heartbeatStarted &&	/* RCS_CMD_START_HEARTBEAT  has been issued */
	    !pRAC->resetInProgress &&	/* soft or hard reset       not      issued */
	    !pRAC->shutDownIssued &&	/* RCS_ADMIN_SHUTDOWN_CARD  not      issued */
	    (hasr & HASR_FIRM_READY) &&	/* firmware is not busy doing something else */
	    ++pRAC->heartbeatCount >= 5) {	/* and finally, it has been a few seconds */
		WRITE_RAC_UCHAR(pRAC->, portAddrHCR, HCR_SOFT_INTR_1);	/* ...since last heartbeat */
		pRAC->heartbeatCount = 0;	/* ...was sent to MegaRac */
	}

}				/* end of racTimerTick() */

/*****************************************************************************
 *
 ****************************************************************************/
static void
racEvent(RAC_INFO * pRAC, RAC_EVENT rawEvent)
{
	RAC_EVENT firm = rawEvent;

	DebugFlagPrint(mega, MEGA_EVENT, ("racEvent      : event=%#x\n", rawEvent));

	/* the os dependent racOsEventProc() should release the current ccb back to the api */

	if (rawEvent == racEventBoardDead) {
		pRAC->okToProcessRCS = FALSE;

		if (pRAC->currentCCB) {
			pRAC->currentCCB->Status = MEGARAC_ERR_DEAD;
			pRAC->currentCCB->Length = 0;
			pRAC->currentCCB = NULL;	/* we've done all we can do */
		}
	}
	/* according to Jose on 07/12/99, most of the racEventFirmwareRequest's are not handled.
	   see megaRacDrvr.h for more info */
	else if (rawEvent == racEventFirmwareRequest) {
		switch (pRAC->firmwareRequestData & 0x0000ffff) {
		case GetDCInfo_FRI:
			firm = racEventGetDCInfo;
			break;
		case GetDCPacket_x_FRI:
			firm = racEventGetDCPacket_X;
			pRAC->firmwareRequestDCPacket = (unsigned short) (pRAC->firmwareRequestData >> 16) & 0x0ffff;
			return;
		case SendDCComplete_FRI:
			firm = racEventSendDCComplete;
			break;
		case SendKeyEvent_FRI:
			firm = racEventSendKey;
			break;
		case SendMouseEvent_FRI:
			firm = racEventSendMouse;
			break;
		case SendAlertToHost_FRI:
			firm = racEventSendAlertToHost;
			break;
		case HOST_OS_SHUTDOWN_REQUEST_FRI:
			firm = racEventRequestHostOsShutdown;
			break;
		case ModeChangedToGraphics_FRI:
			firm = racEventModeChangedToGraphics;
			break;
		case ServiceDYRM_FRI:
			firm = racEventServiceDYRM;
			break;
		case PassThruData_FRI:
			firm = racEventPassThruData;
			break;
		default:
			return;
		}		/* end 'switch' */

		DebugFlagPrint(mega, MEGA_EVENT,
			       ("racEvent      : HIFR_FIRM_REQ: firm=%#x (%s)\n",
				(int) firm, racFriToString((int) firm)));
	}

	/* end 'if/else rawEvent' */
	/* invoke the os-dependent function to service this event */
	if (racOsEventProc)
		(*racOsEventProc) (pRAC, rawEvent, firm);

}				/* end of racEvent() */

