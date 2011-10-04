/*
  * icom.c
  *
  * Copyright (C) 2001 Michael Anderson, IBM Corporation
  *
  * Serial device driver.
  *
  * Based on code from serial.c
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, write to the Free Software
  * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
  *
  */
#define SERIAL_DO_RESTART
#include <linux/module.h>

MODULE_AUTHOR ("Michael Anderson <mjanders@us.ibm.com>");
MODULE_DESCRIPTION ("IBM iSeries Serial IOA driver");
MODULE_SUPPORTED_DEVICE("IBM iSeries 2745, 2771, 2772, 2742, 2793 and 2805 Communications adapters");
MODULE_LICENSE("GPL");

#include <linux/config.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/termios.h>
#include <linux/fs.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/serial.h>

#ifdef CONFIG_PPC_ISERIES

#ifndef CONFIG_PPC64
#include <asm/iSeries/iSeries_VpdInfo.h>
#endif

#include <asm/iSeries/iSeries_pci.h>
#include <asm/iSeries/iSeries_dma.h>
#include <asm/iSeries/HvCallCfg.h>
#endif


/* adapter code loads */
#include "icom.h"
/* icom structure declarations. */
#include <linux/icom_udbg.h>


#define ICOM_TRACE /* enable port trace capabilities */

#define ICOM_DRIVER_NAME "icom"
#define ICOM_VERSION_STR "1.1.1"
#define ICOM_DEV_ID_1    0x0031
#define ICOM_DEV_ID_2    0x0219
#define MAX_ADAPTERS     40
#define NR_PORTS	 (active_adapters * 4)
#define MAX_PORTS        (MAX_ADAPTERS * 4)
#define ASYNC_CLOSING    0x08000000 /* Serial port is closing */
#define ASYNC_HUP_NOTIFY 0x0001 /* Notify getty on hangups and closes 
                                 on the callout port */

static const struct pci_device_id icom_pci_table[] __initdata = {
    {
        vendor: PCI_VENDOR_ID_IBM,
        device: ICOM_DEV_ID_1,
        subvendor: 0xFFFF,
        subdevice: 0xFFFF,
    },
    {
        vendor: PCI_VENDOR_ID_IBM,
        device: ICOM_DEV_ID_2,
        subvendor: PCI_VENDOR_ID_IBM,
        subdevice: 0x021a,
    },
    {
        vendor: PCI_VENDOR_ID_IBM,
        device: ICOM_DEV_ID_2,
        subvendor: PCI_VENDOR_ID_IBM,
        subdevice: 0x0251,
    },
    {
        vendor: PCI_VENDOR_ID_IBM,
        device: ICOM_DEV_ID_2,
        subvendor: PCI_VENDOR_ID_IBM,
        subdevice: 0x0252,
    },
    { }
};
MODULE_DEVICE_TABLE(pci, icom_pci_table);

static int                active_adapters;
static struct tty_driver  serial_driver;
static int                serial_refcount = 0;
static struct tty_struct *serial_table[MAX_PORTS];
static struct termios    *serial_termios[MAX_PORTS];
static struct termios    *serial_termios_locked[MAX_PORTS];

extern struct icom_adapter *icom_adapter_info;

struct mthread
{
    void         *thread;
    u32           task;
    u8            port;
    int           status;
#define STATUS_INIT 0x99999999
#define STATUS_PASS 0 
    u32           tpr;
    u32           error_data[NUM_ERROR_ENTRIES];
};

#define	 IOA_FAILURE	0xFF

static spinlock_t icom_lock;
/*
 Utility functions
 */
static void diag_return_resources(struct icom_adapter *icom_adapter_ptr);
static void return_port_memory(struct icom_port *icom_port_info);
static void icom_wait_until_sent(struct tty_struct *tty, int timeout);
static void do_softint(void *);
static void icom_start(struct tty_struct * tty);
static void icom_flush_buffer(struct tty_struct * tty);
static u32 __init diag_main(struct icom_adapter *icom_adapter_ptr);
#ifdef ICOM_TRACE
static void trace_lock(struct icom_port *, u32 , u32);
static void trace_nolock(struct icom_port *, u32 , u32);
static void trace_mem_mgr(struct icom_port *, u32, u32);
#else
static void trace_lock(struct icom_port *, u32 , u32) {};
static void trace_nolock(struct icom_port *, u32 , u32) {};
static void trace_mem_mgr(struct icom_port *, u32, u32) {};
#endif

#ifdef CONFIG_PPC64
extern int register_ioctl32_conversion(unsigned int cmd,
                                       int (*handler)(unsigned int, unsigned int, unsigned long, struct file *));
extern int unregister_ioctl32_conversion(unsigned int cmd);
#else
static inline int register_ioctl32_conversion(unsigned int cmd,
                                              int (*handler)(unsigned int,
                                                             unsigned int, unsigned long, struct file *))
{
    return 0;
}
static inline int unregister_ioctl32_conversion(unsigned int cmd)
{
    return 0;
}
#endif

/*
 * Context:  Task Level
 * Locks:    none
 */
static int get_port_memory(struct icom_port *icom_port_info)
{
    int            index;
    int            number_of_buffs;
    unsigned long  stgAddr;
    unsigned long  startStgAddr;
    unsigned long  offset;

    trace_nolock(icom_port_info, TRACE_GET_PORT_MEM,0);

    icom_port_info->xmit_buf = (unsigned char *)
        pci_alloc_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                             4096, &icom_port_info->xmit_buf_pci);
                                                                     
    if (!icom_port_info->xmit_buf) {
        printk(KERN_ERR"icom:  ERROR, Can not allocate Transmit buffer\n");
        return -ENOMEM;
    }
    trace_nolock(icom_port_info, TRACE_GET_PORT_MEM,(unsigned long)icom_port_info->xmit_buf);

    icom_port_info->recv_buf = (unsigned char *)
        pci_alloc_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                             4096, &icom_port_info->recv_buf_pci);

    if (!icom_port_info->recv_buf) {
        printk(KERN_ERR"icom:  ERROR, Can not allocate Receive buffer\n");
        return_port_memory(icom_port_info);
        return -ENOMEM;
    }
    trace_nolock(icom_port_info, TRACE_GET_PORT_MEM,(unsigned long)icom_port_info->recv_buf);

    icom_port_info->statStg = (struct statusArea *)
        pci_alloc_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                             4096, &icom_port_info->statStg_pci);

    if (!icom_port_info->statStg) {
        printk(KERN_ERR"icom:  ERROR, Can not allocate Status buffer\n");
        return_port_memory(icom_port_info);
        return -ENOMEM;
    }
    trace_nolock(icom_port_info, TRACE_GET_PORT_MEM,(unsigned long)icom_port_info->statStg);

    icom_port_info->xmitRestart = (u32 *)pci_alloc_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                                                              4096, &icom_port_info->xmitRestart_pci);

    if (!icom_port_info->xmitRestart) {
        printk(KERN_ERR"icom:  ERROR, Can not allocate xmit Restart buffer\n");
        return_port_memory(icom_port_info);
        return -ENOMEM;
    }

    memset(icom_port_info->statStg, 0,4096);

    /* FODs */
    number_of_buffs = NUM_XBUFFS;
    stgAddr = (unsigned long)icom_port_info->statStg;
    startStgAddr = stgAddr;
    for (index = 0; index < number_of_buffs; index++) {
        trace_nolock(icom_port_info, TRACE_FOD_ADDR,stgAddr);
        stgAddr = stgAddr + sizeof(icom_port_info->statStg->xmit[0]);
        if (index < (number_of_buffs - 1))
        {
            icom_port_info->statStg->xmit[index].flags = 0;
            icom_port_info->statStg->xmit[index].leNext = 0;
            icom_port_info->statStg->xmit[index].leNextASD = 0;
            icom_port_info->statStg->xmit[index].leLengthASD = (unsigned short int)cpu_to_le16(XMIT_BUFF_SZ);
            icom_port_info->statStg->xmit[index].leOffsetASD = 0;
            trace_nolock(icom_port_info, TRACE_FOD_ADDR,stgAddr);
            trace_nolock(icom_port_info, TRACE_FOD_XBUFF,(unsigned long)icom_port_info->xmit_buf);
            icom_port_info->statStg->xmit[index].leBuffer = cpu_to_le32(icom_port_info->xmit_buf_pci);
        }
        else if (index == (number_of_buffs - 1)) {
            icom_port_info->statStg->xmit[index].flags = 0;
            icom_port_info->statStg->xmit[index].leNext = 0;
            icom_port_info->statStg->xmit[index].leNextASD = 0;
            icom_port_info->statStg->xmit[index].leLengthASD = (unsigned short int)cpu_to_le16(XMIT_BUFF_SZ);
            icom_port_info->statStg->xmit[index].leOffsetASD = 0;
            trace_nolock(icom_port_info, TRACE_FOD_XBUFF,(unsigned long)icom_port_info->xmit_buf);
            icom_port_info->statStg->xmit[index].leBuffer = cpu_to_le32(icom_port_info->xmit_buf_pci);
        }
        else {
            icom_port_info->statStg->xmit[index].flags = 0;
            icom_port_info->statStg->xmit[index].leNext = 0;
            icom_port_info->statStg->xmit[index].leNextASD = 0;
            icom_port_info->statStg->xmit[index].leLengthASD = 0;
            icom_port_info->statStg->xmit[index].leOffsetASD = 0;
            icom_port_info->statStg->xmit[index].leBuffer = 0;
        }
    }
    /* FIDs */
    startStgAddr = stgAddr;

    /* fill in every entry, even if no buffer */
    number_of_buffs = NUM_RBUFFS;
    for (index = 0; index < number_of_buffs; index++) {
        trace_nolock(icom_port_info, TRACE_FID_ADDR,stgAddr);
        stgAddr = stgAddr + sizeof(icom_port_info->statStg->rcv[0]);
        icom_port_info->statStg->rcv[index].leLength = 0;
        icom_port_info->statStg->rcv[index].WorkingLength = (unsigned short int)cpu_to_le16(RCV_BUFF_SZ); 
        if (index < (number_of_buffs - 1)) {
            offset = stgAddr - (unsigned long)icom_port_info->statStg;
            icom_port_info->statStg->rcv[index].leNext = (unsigned long)cpu_to_le32(icom_port_info->statStg_pci + offset);
            trace_nolock(icom_port_info, TRACE_FID_RBUFF,(unsigned long)icom_port_info->recv_buf);
            icom_port_info->statStg->rcv[index].leBuffer = cpu_to_le32(icom_port_info->recv_buf_pci);
        }
        else if (index == (number_of_buffs - 1)) {
            offset = startStgAddr - (unsigned long)icom_port_info->statStg;
            icom_port_info->statStg->rcv[index].leNext = (unsigned long)cpu_to_le32(icom_port_info->statStg_pci + offset);
            trace_nolock(icom_port_info, TRACE_FID_RBUFF,(unsigned long)icom_port_info->recv_buf + 2048);
            icom_port_info->statStg->rcv[index].leBuffer = cpu_to_le32(icom_port_info->recv_buf_pci + 2048);
        }
        else {
            icom_port_info->statStg->rcv[index].leNext = 0;
            icom_port_info->statStg->rcv[index].leBuffer = 0;
        }
    }

    return 0;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void return_port_memory(struct icom_port *icom_port_info)
{
    trace_nolock(icom_port_info, TRACE_RET_PORT_MEM,0);
    if (icom_port_info->recv_buf) {
        pci_free_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                            4096, icom_port_info->recv_buf,
                            icom_port_info->recv_buf_pci);
        icom_port_info->recv_buf = 0;
    }
    if (icom_port_info->xmit_buf) {
        pci_free_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                            4096, icom_port_info->xmit_buf,
                            icom_port_info->xmit_buf_pci);
        icom_port_info->xmit_buf = 0;
    }
    if (icom_port_info->statStg) {
        pci_free_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                            4096, icom_port_info->statStg,
                            icom_port_info->statStg_pci);
        icom_port_info->statStg = 0;
    }

    if (icom_port_info->xmitRestart) {
        pci_free_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                            4096, icom_port_info->xmitRestart,
                            icom_port_info->xmitRestart_pci);
        icom_port_info->xmitRestart = 0;
    }
    trace_mem_mgr(icom_port_info,TRACE_RET_MEM,0);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void stop_processor(struct icom_port *icom_port_info)
{
    unsigned long  temp;
    unsigned long  flags;

    spin_lock_irqsave(&icom_lock, flags);

    switch (icom_port_info->port) {
        case 0:
            temp = readl(&icom_port_info->global_reg->control);
            temp = (temp & ~ICOM_CONTROL_START_A) | ICOM_CONTROL_STOP_A;
            writel(temp,&icom_port_info->global_reg->control);
            trace_lock(icom_port_info, TRACE_STOP_PROC_A,0);
            break;
        case 1:
            temp = readl(&icom_port_info->global_reg->control);
            temp = (temp & ~ICOM_CONTROL_START_B) | ICOM_CONTROL_STOP_B;
            writel(temp,&icom_port_info->global_reg->control);
            trace_lock(icom_port_info, TRACE_STOP_PROC_B,0);
            break;
        case 2:
            temp = readl(&icom_port_info->global_reg->control_2);
            temp = (temp & ~ICOM_CONTROL_START_C) | ICOM_CONTROL_STOP_C;
            writel(temp,&icom_port_info->global_reg->control_2);
            trace_lock(icom_port_info, TRACE_STOP_PROC_C,0);
            break;
        case 3:
            temp = readl(&icom_port_info->global_reg->control_2);
            temp = (temp & ~ICOM_CONTROL_START_D) | ICOM_CONTROL_STOP_D;
            writel(temp,&icom_port_info->global_reg->control_2);
            trace_lock(icom_port_info, TRACE_STOP_PROC_D,0);
            break;
        default:
            printk(KERN_WARNING"icom:  ERROR:  invalid port assignment\n");
    }
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void start_processor(struct icom_port *icom_port_info)
{
    unsigned long  temp;
    unsigned long  flags;

    spin_lock_irqsave(&icom_lock, flags);

    switch (icom_port_info->port) {
        case 0:
            temp = readl(&icom_port_info->global_reg->control);
            temp = (temp & ~ICOM_CONTROL_STOP_A) | ICOM_CONTROL_START_A;
            writel(temp,&icom_port_info->global_reg->control);
            trace_lock(icom_port_info, TRACE_START_PROC_A,0);
            break;
        case 1:
            temp = readl(&icom_port_info->global_reg->control);
            temp = (temp & ~ICOM_CONTROL_STOP_B) | ICOM_CONTROL_START_B;
            writel(temp,&icom_port_info->global_reg->control);
            trace_lock(icom_port_info, TRACE_START_PROC_B,0);
            break;
        case 2:
            temp = readl(&icom_port_info->global_reg->control_2);
            temp = (temp & ~ICOM_CONTROL_STOP_C) | ICOM_CONTROL_START_C;
            writel(temp,&icom_port_info->global_reg->control_2);
            trace_lock(icom_port_info, TRACE_START_PROC_C,0);
            break;
        case 3:
            temp = readl(&icom_port_info->global_reg->control_2);
            temp = (temp & ~ICOM_CONTROL_STOP_D) | ICOM_CONTROL_START_D;
            writel(temp,&icom_port_info->global_reg->control_2);
            trace_lock(icom_port_info, TRACE_START_PROC_D,0);
            break;
        default:
            printk(KERN_WARNING"icom:  ERROR: invalid port assignment\n");
    }
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void load_code (struct icom_port *icom_port_info)
{
    char               *iram_ptr;
    int                index;
    int                status = 0;
    char               *dram_ptr = (char *)icom_port_info->dram;
    dma_addr_t         temp_pci;
    unsigned char      *new_page;
    unsigned char      cable_id;

    trace_mem_mgr(icom_port_info,TRACE_GET_MEM,0);

    /* Clear out any pending interrupts */
    writew(0x3FFF,(void *)icom_port_info->int_reg);

    trace_nolock(icom_port_info, TRACE_CLEAR_INTERRUPTS,0);

    /* Stop processor */
    stop_processor(icom_port_info);

    /* Zero out DRAM */
    for (index = 0; index < 512; index++) {
        writeb(0x00,&dram_ptr[index]);
    }

    /* Load Call Setup into Adapter */
    iram_ptr = (char *)icom_port_info->dram + ICOM_IRAM_OFFSET;
    for (index = 0; index < sizeof(callSetup); index++) {
        writeb(callSetup[index],&iram_ptr[index]);
    }

    /* Load Resident DCE portion of Adapter */
    iram_ptr = (char *) icom_port_info->dram + ICOM_IRAM_OFFSET +
        ICOM_DCE_IRAM_OFFSET;

    /* Load the RV dce code */
    for (index = 0; index < sizeof(resRVdce); index++) {
        writeb(resRVdce[index],&iram_ptr[index]);
    }

    /* Set Hardware level */
    if ((icom_adapter_info[icom_port_info->adapter].version | ADAPTER_V2) == ADAPTER_V2) {
        writeb(V2_HARDWARE,&(icom_port_info->dram->misc_flags));
    }

    /* Start the processor in Adapter */
    start_processor(icom_port_info);

    writeb((HDLC_PPP_PURE_ASYNC | HDLC_FF_FILL),&(icom_port_info->dram->HDLCConfigReg));
    writeb(0x04,&(icom_port_info->dram->FlagFillIdleTimer)); /* 0.5 seconds */
    writeb(0x00,&(icom_port_info->dram->CmdReg));
    writeb(0x10,&(icom_port_info->dram->async_config3));
    writeb((ICOM_ACFG_DRIVE1 | ICOM_ACFG_NO_PARITY | ICOM_ACFG_8BPC |
                 ICOM_ACFG_1STOP_BIT),&(icom_port_info->dram->async_config2));

    /*Set up data in icom DRAM to indicate where personality
     *code is located and its length.
     */
    new_page = (unsigned char *)
        pci_alloc_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                             4096, &temp_pci);

    if (!new_page) {
        printk(KERN_ERR"icom:  ERROR, Can not allocate buffer\n");
        status = -1;
        goto load_code_exit;
    }

    for (index = 0; index < sizeof(funcLoad); index++) {
        new_page[index] = funcLoad[index];
    }

    writeb((char)(sizeof(funcLoad)/16),&icom_port_info->dram->mac_length);
    writel(temp_pci,&icom_port_info->dram->mac_load_addr);

    /*Setting the syncReg to 0x80 causes adapter to start downloading
     the personality code into adapter instruction RAM.
     Once code is loaded, it will begin executing and, based on
     information provided above, will start DMAing data from
     shared memory to adapter DRAM.
     */
    writeb(START_DOWNLOAD,&icom_port_info->dram->sync);

    /* Wait max 1 Sec for data download and processor to start */
    for (index = 0; index < 10; index++ ) {
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/10);
        if (readb(&icom_port_info->dram->misc_flags) & ICOM_HDW_ACTIVE) break;
    }

    if (index == 100) status = -1;

    pci_free_consistent(icom_adapter_info[icom_port_info->adapter].pci_dev,
                        4096, new_page, temp_pci);

    /*
     * check Cable ID
     */
    cable_id = readb(&icom_port_info->dram->cable_id);

    if (cable_id & ICOM_CABLE_ID_VALID) {
        /* Get cable ID into the lower 4 bits (standard form) */
	printk(KERN_INFO"icom: cable_id (%d) -> ",cable_id);
        cable_id = (cable_id & ICOM_CABLE_ID_MASK) >> 4;
	printk(KERN_INFO"icom: cable_id (%d)\n",cable_id);
        icom_port_info->cable_id = cable_id;
    }
    else {
        icom_port_info->cable_id = NO_CABLE;
    }

    load_code_exit:

    if (status != 0) {
        /* Clear out any pending interrupts */
        writew(0x3FFF,(void *)icom_port_info->int_reg);

        /* Turn off port */
        writeb(ICOM_DISABLE,&(icom_port_info->dram->disable));

        /* Stop processor */
        stop_processor(icom_port_info);

        /* Fail the port */
        if (cable_id != NO_CABLE) {
            printk(KERN_WARNING"icom:  Error, minor number %d port not opertional\n", icom_port_info->minor_number);
            icom_port_info->passed_diags = 0;
        }
    }
}

/*
 * This routine is called to set the port to match
 * the specified baud rate for a serial port.
 *
 * Context:  Task Level
 * Locks:    icom_lock
 */
static void change_speed(struct icom_port *icom_port_info,
                         struct termios *old_termios)
{
    int	       baud;
    unsigned       cflag;
    int	       bits;
    char           new_config2;
    char           new_config3;
    char           tmp_byte;
    int            index;
    int            rcv_buff,xmit_buff;
    unsigned long  offset;

    trace_lock(icom_port_info, TRACE_CHANGE_SPEED | TRACE_TIME,jiffies);

    if (!icom_port_info->tty || !icom_port_info->tty->termios) {
        printk(KERN_ERR"icom:  tty structs not found\n");
        return;
    }
    cflag = icom_port_info->tty->termios->c_cflag;

    new_config2 = ICOM_ACFG_DRIVE1;

    /* byte size and parity */
    switch (cflag & CSIZE) {
        case CS5: /* 5 bits/char */
            new_config2 |= ICOM_ACFG_5BPC;
            bits = 7;
            break;
        case CS6: /* 6 bits/char */
            new_config2 |= ICOM_ACFG_6BPC;
            bits = 8;
            break;
        case CS7: /* 7 bits/char */
            new_config2 |= ICOM_ACFG_7BPC;
            bits = 9;
            break;
        case CS8: /* 8 bits/char */
            new_config2 |= ICOM_ACFG_8BPC;
            bits = 10;
            break;
        default:  bits = 10;  break;
    }
    if (cflag & CSTOPB) {
        /* 2 stop bits */
        new_config2 |= ICOM_ACFG_2STOP_BIT;
        bits++;
    }
    if (cflag & PARENB) {
        /* parity bit enabled */
        new_config2 |= ICOM_ACFG_PARITY_ENAB;
        trace_lock(icom_port_info, TRACE_PARENB,0);
        bits++;
    }
    if (cflag & PARODD) {
        /* odd parity */
        new_config2 |= ICOM_ACFG_PARITY_ODD;
        trace_lock(icom_port_info, TRACE_PARODD,0);
    }

    /* Determine divisor based on baud rate */
    baud = tty_get_baud_rate(icom_port_info->tty);
    if (!baud)
        baud = 9600;	/* B0 transition handled in rs_set_termios */

    for (index = 0; index < BAUD_TABLE_LIMIT; index++) {
        if (icom_acfg_baud[index] == baud) {
            new_config3 = index;
            break;
        }
    }

    icom_port_info->timeout = XMIT_BUFF_SZ*HZ*bits/baud;
    icom_port_info->timeout += HZ/50;		/* Add .02 seconds of slop */

    /* CTS flow control flag and modem status interrupts */
    if (cflag & CRTSCTS) {
        icom_port_info->flags |= ASYNC_CTS_FLOW;
        tmp_byte = readb(&(icom_port_info->dram->HDLCConfigReg));
        tmp_byte |= HDLC_HDW_FLOW;
        writeb(tmp_byte, &(icom_port_info->dram->HDLCConfigReg));
    }
    else {
        icom_port_info->flags &= ~ASYNC_CTS_FLOW;
        tmp_byte = readb(&(icom_port_info->dram->HDLCConfigReg));
        tmp_byte &= ~HDLC_HDW_FLOW;
        writeb(tmp_byte, &(icom_port_info->dram->HDLCConfigReg));
    }
    if (cflag & CLOCAL)
        icom_port_info->flags &= ~ASYNC_CHECK_CD;
    else
        icom_port_info->flags |= ASYNC_CHECK_CD;

    /*
     * Set up parity check flag
     */
    icom_port_info->read_status_mask = SA_FLAGS_OVERRUN | SA_FL_RCV_DONE;
    if (I_INPCK(icom_port_info->tty))
        icom_port_info->read_status_mask |= SA_FLAGS_FRAME_ERROR | SA_FLAGS_PARITY_ERROR;

    if (I_BRKINT(icom_port_info->tty) || I_PARMRK(icom_port_info->tty))
        icom_port_info->read_status_mask |= SA_FLAGS_BREAK_DET;

    /*
     * Characters to ignore
     */
    icom_port_info->ignore_status_mask = 0;
    if (I_IGNPAR(icom_port_info->tty))
        icom_port_info->ignore_status_mask |= SA_FLAGS_PARITY_ERROR | SA_FLAGS_FRAME_ERROR;
    if (I_IGNBRK(icom_port_info->tty)) {
        icom_port_info->ignore_status_mask |= SA_FLAGS_BREAK_DET;
        /*
         * If we're ignore parity and break indicators, ignore 
         * overruns too.  (For real raw support).
         */
        if (I_IGNPAR(icom_port_info->tty))
            icom_port_info->ignore_status_mask |= SA_FLAGS_OVERRUN;
    }

    /*
     * !!! ignore all characters if CREAD is not set
     */
    if ((cflag & CREAD) == 0)
        icom_port_info->ignore_status_mask |= SA_FL_RCV_DONE;

    /* Turn off Receiver to prepare for reset */
    writeb(CMD_RCV_DISABLE,&icom_port_info->dram->CmdReg);

    spin_unlock_irq(&icom_lock);

    /* allow 1 second for receive operations to complete */
    for (index = 0; index < 10; index++) {
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/10);

        if (readb(&icom_port_info->dram->PrevCmdReg) == 0x00) {
            break;
        }
    }
    spin_lock_irq(&icom_lock);

    /* clear all current buffers of data */
    for (rcv_buff = 0; rcv_buff < NUM_RBUFFS; rcv_buff++) {
        icom_port_info->statStg->rcv[rcv_buff].flags = 0;
        icom_port_info->statStg->rcv[rcv_buff].leLength = 0;
        icom_port_info->statStg->rcv[rcv_buff].WorkingLength = (unsigned short int)cpu_to_le16(RCV_BUFF_SZ);
    }

    for (xmit_buff = 0; xmit_buff < NUM_XBUFFS;  xmit_buff++) {
        icom_port_info->statStg->xmit[xmit_buff].flags = 0;
    }

    /* activate changes and start xmit and receiver here */
    /* Enable the receiver */
    writeb(new_config3,&(icom_port_info->dram->async_config3));
    writeb(new_config2,&(icom_port_info->dram->async_config2));
    tmp_byte = readb(&(icom_port_info->dram->HDLCConfigReg));
    tmp_byte |= HDLC_PPP_PURE_ASYNC | HDLC_FF_FILL;
    writeb(tmp_byte,&(icom_port_info->dram->HDLCConfigReg));
    writeb(0x04, &(icom_port_info->dram->FlagFillIdleTimer)); /* 0.5 seconds */
    writeb(0xFF, &(icom_port_info->dram->ier)); /* enable modem signal interrupts */

    /* reset processor */
    writeb(CMD_RESTART,&icom_port_info->dram->CmdReg);
    spin_unlock_irq(&icom_lock);

    /* allow 1 second to allow reset operations to complete */
    for (index = 0; index < 10; index++) {
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/10);

        if (readb(&icom_port_info->dram->CmdReg) == 0x00) {
            break;
        }
    }
    spin_lock_irq(&icom_lock);

    /* Enable Transmitter and Reciever */
    offset = (unsigned long)&icom_port_info->statStg->rcv[0] - (unsigned long)icom_port_info->statStg;
    writel(icom_port_info->statStg_pci + offset,&icom_port_info->dram->RcvStatusAddr);
    icom_port_info->next_rcv = 0;
    icom_port_info->put_length = 0;
    *icom_port_info->xmitRestart = 0;
    writel(icom_port_info->xmitRestart_pci,&icom_port_info->dram->XmitStatusAddr);
    trace_lock(icom_port_info, TRACE_XR_ENAB,0);
    writeb(CMD_XMIT_RCV_ENABLE,&icom_port_info->dram->CmdReg);
}

/*
 * Context:  Task Level
 * Locks:    icom_lock
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
                           struct icom_port *icom_port_info)
{
    DECLARE_WAITQUEUE(wait, current);
    int		retval;
    int		do_clocal = 0, extra_count = 0;

    /*
     * If the device is in the middle of being closed, then block
     * until it's done, and then try again.
     */
    if (tty_hung_up_p(filp) ||
        (icom_port_info->flags & ASYNC_CLOSING)) {

        add_wait_queue(&icom_port_info->close_wait, &wait);
        current->state = TASK_INTERRUPTIBLE;

        if (icom_port_info->flags & ASYNC_CLOSING) {
            spin_unlock_irq(&icom_lock);
            schedule();
            spin_lock_irq(&icom_lock);
        }

        current->state = TASK_RUNNING;
        remove_wait_queue(&icom_port_info->close_wait, &wait);

#ifdef SERIAL_DO_RESTART
        return ((icom_port_info->flags & ASYNC_HUP_NOTIFY) ?
                -EAGAIN : -ERESTARTSYS);
#else
        return -EAGAIN;
#endif
    }

    /*
     * If this is a callout device, then just make sure the normal
     * device isn't being used.
     */
    if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
        if (icom_port_info->flags & ASYNC_NORMAL_ACTIVE)
            return -EBUSY;
        if ((icom_port_info->flags & ASYNC_CALLOUT_ACTIVE) &&
            (icom_port_info->flags & ASYNC_SESSION_LOCKOUT) &&
            (icom_port_info->session != current->session))
            return -EBUSY;
        if ((icom_port_info->flags & ASYNC_CALLOUT_ACTIVE) &&
            (icom_port_info->flags & ASYNC_PGRP_LOCKOUT) &&
            (icom_port_info->pgrp != current->pgrp))
            return -EBUSY;

        icom_port_info->flags |= ASYNC_CALLOUT_ACTIVE;
        return 0;
    }

    /*
     * If non-blocking mode is set, or the port is not enabled,
     * then make the check up front and then exit.
     */
    if ((filp->f_flags & O_NONBLOCK) ||
        (tty->flags & (1 << TTY_IO_ERROR))) {
        if (icom_port_info->flags & ASYNC_CALLOUT_ACTIVE)
            return -EBUSY;

        icom_port_info->flags |= ASYNC_NORMAL_ACTIVE;
        return 0;
    }

    if (icom_port_info->flags & ASYNC_CALLOUT_ACTIVE) {
        if (icom_port_info->normal_termios.c_cflag & CLOCAL)
            do_clocal = 1;
    }
    else {
        if (tty->termios->c_cflag & CLOCAL)
            do_clocal = 1;
    }

    /*
     * Block waiting for the carrier detect and the line to become
     * free (i.e., not in use by the callout).  While we are in
     * this loop, open_active_count is dropped by one, so that
     * rs_close() knows when to free things.  We restore it upon
     * exit, either normal or abnormal.
     */
    retval = 0;
    add_wait_queue(&icom_port_info->open_wait, &wait);

    if (!tty_hung_up_p(filp)) {
        extra_count = 1;
        icom_port_info->open_active_count--;
    }
    icom_port_info->blocked_open++;

    while (1) {
        if (!(icom_port_info->flags & ASYNC_CALLOUT_ACTIVE) &&
            (tty->termios->c_cflag & CBAUD)) {
            /* raise DTR and RTS */
            trace_lock(icom_port_info, TRACE_RAISE_DTR_RTS,0);
            writeb(0xC0,&icom_port_info->dram->osr);
        }
        current->state = TASK_INTERRUPTIBLE;
        if (tty_hung_up_p(filp) ||
            !(icom_port_info->flags & ASYNC_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
            if (icom_port_info->flags & ASYNC_HUP_NOTIFY)
                retval = -EAGAIN;
            else
                retval = -ERESTARTSYS;	
#else
            retval = -EAGAIN;
#endif
            break;
        }

        if (!(icom_port_info->flags & ASYNC_CALLOUT_ACTIVE) &&
            !(icom_port_info->flags & ASYNC_CLOSING) &&
            (do_clocal || (readb(&icom_port_info->dram->isr) & ICOM_DCD))) /* Carrier Detect */
            break;
        if (signal_pending(current)) {
            retval = -ERESTARTSYS;
            break;
        }
        printk(KERN_INFO"icom:  WAIT for CD\n");
        spin_unlock_irq(&icom_lock);
        schedule();
        spin_lock_irq(&icom_lock);
    }

    current->state = TASK_RUNNING;
    remove_wait_queue(&icom_port_info->open_wait, &wait);

    if (extra_count)
        icom_port_info->open_active_count++;
    icom_port_info->blocked_open--;

    if (retval)
        return retval;

    icom_port_info->flags |= ASYNC_NORMAL_ACTIVE;
    return 0;
}

/*
 * Context:  Task Level
 * Locks:    icom_lock
 */
static int startup(struct icom_port *icom_port_info)
{
    int	       retval=0;
    unsigned long  temp;
    unsigned char  cable_id, raw_cable_id;
    unsigned long  flags;

    trace_lock(icom_port_info, TRACE_STARTUP,0);

    if (icom_port_info->flags & ASYNC_INITIALIZED) {
        goto errout;
    }

    if (icom_port_info->dram == 0x00000000) {
        /* should NEVER be zero */
        printk(KERN_ERR"icom:  Unusable Port, minor number (%d) port configuration missing\n",icom_port_info->minor_number);
        return -ENODEV;
    }

    /*
     * check Cable ID
     */
    raw_cable_id = readb(&icom_port_info->dram->cable_id);
    trace_lock(icom_port_info, TRACE_CABLE_ID,raw_cable_id);

    /* Get cable ID into the lower 4 bits (standard form) */
    cable_id = (raw_cable_id & ICOM_CABLE_ID_MASK) >> 4;

    /* Check Cable ID is RS232 */
/*  if (!(raw_cable_id & ICOM_CABLE_ID_VALID) || (cable_id != RS232_CABLE)) */
    if (0)
    {
        /* reload adapter code, pick up any potential changes in cable id */
        if (icom_port_info->load_in_progress) {
            printk(KERN_INFO"icom:  Unusable Port, minor number (%d) currently being initialized by another task\n", icom_port_info->minor_number);
            return -ENODEV;
        }
        icom_port_info->load_in_progress = 1;
        spin_unlock_irq(&icom_lock);

        load_code(icom_port_info);

        spin_lock_irq(&icom_lock);
        icom_port_info->load_in_progress = 0;

        /* still no sign of RS232, error out */
        if (icom_port_info->cable_id != RS232_CABLE) {
            printk(KERN_INFO"icom:  Unusable Port, minor number (%d) incorrect cable attached, only RS232 cables permitted\n",icom_port_info->minor_number);
            return -ENODEV;
        }
    }

    /*
     * set appropriate modem signals
     */
    if (icom_port_info->tty->termios->c_cflag & CBAUD) {
        /* raise DTR and RTS */
        trace_lock(icom_port_info, TRACE_RAISE_DTR_RTS,0);
        writeb(0xC0,&icom_port_info->dram->osr);
    }

    /*
     * Finally, clear and  enable interrupts
     */
    switch (icom_port_info->port) {
        case 0:
            /* Clear out any pending interrupts */
            writew(0x00FF,(void *)icom_port_info->int_reg);

            /* Enable interrupts for first port */
            trace_lock(icom_port_info, TRACE_ENABLE_INTERRUPTS_PA,0);
            temp = readl(&icom_port_info->global_reg->int_mask);
            writel((temp & ~ICOM_INT_MASK_PRC_A),&icom_port_info->global_reg->int_mask);
            break;
        case 1:
            /* Clear out any pending interrupts */
            writew(0x3F00,(void *)icom_port_info->int_reg);

            /* Enable interrupts for second port */
            trace_lock(icom_port_info, TRACE_ENABLE_INTERRUPTS_PB,0);
            temp = readl(&icom_port_info->global_reg->int_mask);
            writel((temp & ~ICOM_INT_MASK_PRC_B),&icom_port_info->global_reg->int_mask);
            break;
        case 2:
            /* Clear out any pending interrupts */
            writew(0x00FF,(void *)icom_port_info->int_reg);

            /* Enable interrupts for first port */
            trace_lock(icom_port_info, TRACE_ENABLE_INTERRUPTS_PC,0);
            temp = readl(&icom_port_info->global_reg->int_mask_2);
            writel((temp & ~ICOM_INT_MASK_PRC_C),&icom_port_info->global_reg->int_mask_2);
            break;
        case 3:
            /* Clear out any pending interrupts */
            writew(0x3F00,(void *)icom_port_info->int_reg);

            /* Enable interrupts for second port */
            trace_lock(icom_port_info, TRACE_ENABLE_INTERRUPTS_PD,0);
            temp = readl(&icom_port_info->global_reg->int_mask_2);
            writel((temp & ~ICOM_INT_MASK_PRC_D),&icom_port_info->global_reg->int_mask_2);
            break;
        default:
            printk(KERN_WARNING"icom:  ERROR:  Invalid port defined\n");
    }

    if (icom_port_info->tty)
        clear_bit(TTY_IO_ERROR, &icom_port_info->tty->flags);

    /*
     * Set up the tty->alt_speed kludge
     */
    if (icom_port_info->tty) {
        if ((icom_port_info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
            icom_port_info->tty->alt_speed = 57600;
        if ((icom_port_info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
            icom_port_info->tty->alt_speed = 115200;
        if ((icom_port_info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
            icom_port_info->tty->alt_speed = 230400;
        if ((icom_port_info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
            icom_port_info->tty->alt_speed = 460800;
    }

    /*
     * and set the speed of the serial port
     */
    change_speed(icom_port_info, 0);

    icom_port_info->flags |= ASYNC_INITIALIZED;

    return 0;

    errout:
        return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 *
 * Context:  Task Level
 * Locks:    icom_lock
 */
static void shutdown(struct icom_port * icom_port_info)
{
    unsigned long  temp;
    unsigned char  cmdReg;

    trace_lock(icom_port_info, TRACE_SHUTDOWN | TRACE_TIME,jiffies);

    if (!(icom_port_info->flags & ASYNC_INITIALIZED))
        return;

    /*
     * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
     * here so the queue might never be waken up
     */
    wake_up_interruptible(&icom_port_info->delta_msr_wait);

    /*
     * disable all interrupts
     */
    switch (icom_port_info->port) {
        case 0:
            trace_lock(icom_port_info, TRACE_DIS_INTERRUPTS_PA,0);
            temp = readl(&icom_port_info->global_reg->int_mask);
            writel((temp | ICOM_INT_MASK_PRC_A),&icom_port_info->global_reg->int_mask);
            break;
        case 1:
            trace_lock(icom_port_info, TRACE_DIS_INTERRUPTS_PB,0);
            temp = readl(&icom_port_info->global_reg->int_mask);
            writel((temp | ICOM_INT_MASK_PRC_B),&icom_port_info->global_reg->int_mask);
            break;
        case 2:
            trace_lock(icom_port_info, TRACE_DIS_INTERRUPTS_PC,0);
            temp = readl(&icom_port_info->global_reg->int_mask_2);
            writel((temp | ICOM_INT_MASK_PRC_C),&icom_port_info->global_reg->int_mask_2);
            break;
        case 3:
            trace_lock(icom_port_info, TRACE_DIS_INTERRUPTS_PD,0);
            temp = readl(&icom_port_info->global_reg->int_mask_2);
            writel((temp | ICOM_INT_MASK_PRC_D),&icom_port_info->global_reg->int_mask_2);
            break;
        default:
            printk(KERN_WARNING"icom:  ERROR:  Invalid port assignment\n");
    }

    /*
     * disable break condition
     */
    cmdReg = readb(&icom_port_info->dram->CmdReg);
    if ((cmdReg | CMD_SND_BREAK) == CMD_SND_BREAK) {
        writeb(cmdReg & ~CMD_SND_BREAK,&icom_port_info->dram->CmdReg);
    }

    if (!icom_port_info->tty || (icom_port_info->tty->termios->c_cflag & HUPCL)) {
        /* drop DTR and RTS */
        trace_lock(icom_port_info, TRACE_DROP_DTR_RTS,0);
        writeb(0x00,&icom_port_info->dram->osr);
    }

    if (icom_port_info->tty)
        set_bit(TTY_IO_ERROR, &icom_port_info->tty->flags);

    icom_port_info->flags &= ~ASYNC_INITIALIZED;
}

/*
 * Open port
 *
 * Context:  Task Level
 * Locks:    none
 */
static int icom_open(struct tty_struct * tty, struct file * filp)
{
    DECLARE_WAITQUEUE(wait, current);
    int               line;
    int               adapter_entry;
    int               port_entry;
    struct icom_port *icom_port_info;
    int               retval;
    unsigned long     flags;

    /*
     Minor Number
     _ _ _ _ b (lower nibble)
     ___ ___
     |   |
     |   - port number (lowest 2 bits is port identifier)
     - adapter number (remaining higher order bits identify adapter #)
     */

    MOD_INC_USE_COUNT;
    line = MINOR(tty->device) - tty->driver.minor_start;
    if ((line < 0) || (line >= NR_PORTS)) {
        printk(KERN_WARNING"icom:  Invalid Minor number (%d) on open\n",MINOR(tty->device));
        MOD_DEC_USE_COUNT;
        return -ENODEV;
    }

    adapter_entry = (line & 0xFFFC) >> 2; /* shift adapter # into position */
    port_entry = line & 0x0003; /* mask of port number */

    if ((port_entry == 1) &&
        (icom_adapter_info[adapter_entry].version == ADAPTER_V2) &&
        (icom_adapter_info[adapter_entry].subsystem_id != FOUR_PORT_MODEL)) {
        port_entry = 2;
    }
    icom_port_info = &icom_adapter_info[adapter_entry].port_info[port_entry];

    if (icom_port_info->status != ICOM_PORT_ACTIVE) {
        printk(KERN_WARNING"icom:  Unusable Port, minor number (%d) invalid minor number on open\n",MINOR(tty->device));
        MOD_DEC_USE_COUNT;
        return -ENODEV;
    }

    if (!icom_port_info->passed_diags) {
        printk(KERN_WARNING"icom:  Unusable Port, minor number (%d) failed diagnostic checks, see previously logged messages\n",MINOR(tty->device));
        MOD_DEC_USE_COUNT;
        return -ENODEV;
    }

    trace_nolock(icom_port_info, TRACE_DEVICE_NUMB,tty->device);
    tty->driver_data = icom_port_info;
    icom_port_info->tty = tty;
    spin_lock_irqsave(&icom_lock,flags);
    icom_port_info->open_active_count++;

    /*
     * If the port is the middle of closing, bail out now
     */
    if (tty_hung_up_p(filp) ||
        (icom_port_info->flags & ASYNC_CLOSING)) {

        MOD_DEC_USE_COUNT;
        icom_port_info->open_active_count--;
        spin_unlock_irqrestore(&icom_lock,flags);

        add_wait_queue(&icom_port_info->close_wait, &wait);
        current->state = TASK_INTERRUPTIBLE;

        if (icom_port_info->flags & ASYNC_CLOSING)
            schedule();

        current->state = TASK_RUNNING;
        remove_wait_queue(&icom_port_info->close_wait, &wait);

#ifdef SERIAL_DO_RESTART
        return ((icom_port_info->flags & ASYNC_HUP_NOTIFY) ?
                -EAGAIN : -ERESTARTSYS);
#else
        return -EAGAIN;
#endif
    }

    /*
     * Start up serial port
     */
    retval = startup(icom_port_info);

    if (retval) {
        /* reset open variables */
        icom_port_info->open_active_count--;
        trace_lock(icom_port_info, TRACE_STARTUP_ERROR,0);
        spin_unlock_irqrestore(&icom_lock,flags);
        return retval;
    }

    retval = block_til_ready(tty, filp, icom_port_info);
    if (retval) {
        icom_port_info->open_active_count--;
        spin_unlock_irqrestore(&icom_lock,flags);
        return retval;
    }

    if ((icom_port_info->open_active_count == 1) &&
        (icom_port_info->flags & ASYNC_SPLIT_TERMIOS)) {
        if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
            *tty->termios = icom_port_info->normal_termios;
        else 
            *tty->termios = icom_port_info->callout_termios;
        change_speed(icom_port_info, 0);
    }

    icom_port_info->session = current->session;
    icom_port_info->pgrp = current->pgrp;
    spin_unlock_irqrestore(&icom_lock,flags);
    return 0;
}

/*
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary.
 *
 * Context:  Task Level
 * Locks:    none
 */
static void icom_close(struct tty_struct * tty, struct file * filp)
{
    struct icom_port *icom_port_info;
    unsigned long     flags;
    unsigned char     cmdReg;


    if (!tty) {
        printk(KERN_WARNING"icom:  icom_close - no tty\n");
        return;
    }

    icom_port_info = (struct icom_port *)tty->driver_data;
    if (!icom_port_info) {
        printk(KERN_WARNING"icom:  icom_close - no tty->driver_data\n");
        return;
    }

    trace_nolock(icom_port_info, TRACE_CLOSE,0);

    if (tty_hung_up_p(filp)) {
        trace_nolock(icom_port_info, TRACE_CLOSE_HANGUP,0);
        MOD_DEC_USE_COUNT;
        return;
    }

    spin_lock_irqsave(&icom_lock,flags);
    if ((atomic_read(&tty->count) == 1) && (icom_port_info->open_active_count != 1)) {
        /*
         * Uh, oh.  tty->count is 1, which means that the tty
         * structure will be freed.  open_active_count should always
         * be one in these conditions.  If it's greater than
         * one, we've got real problems, since it means the
         * serial port won't be shutdown.
         */
        icom_port_info->open_active_count = 1;
    }

    if (--icom_port_info->open_active_count < 0) {
        icom_port_info->open_active_count = 0;
    }

    if (icom_port_info->open_active_count) {
        trace_lock(icom_port_info, TRACE_OPEN_ACTIVE,0);
        MOD_DEC_USE_COUNT;
        spin_unlock_irqrestore(&icom_lock,flags);
        return;
    }
    icom_port_info->flags |= ASYNC_CLOSING;

    /*
     * Save the termios structure, since this port may have
     * separate termios for callout and dialin.
     */
    if (icom_port_info->flags & ASYNC_NORMAL_ACTIVE)
        icom_port_info->normal_termios = *tty->termios;
    if (icom_port_info->flags & ASYNC_CALLOUT_ACTIVE)
        icom_port_info->callout_termios = *tty->termios;

    /*
     * Now we wait for the transmit buffer to clear; and we notify 
     * the line discipline to only process XON/XOFF characters.
     */
    tty->closing = 1;
    if (icom_port_info->closing_wait != ASYNC_CLOSING_WAIT_NONE) {
        spin_unlock_irqrestore(&icom_lock,flags);
        tty_wait_until_sent(tty, icom_port_info->closing_wait);
        spin_lock_irqsave(&icom_lock,flags);
    }

    /*
     * At this point we stop accepting input.  To do this, we
     * disable the receive line status interrupts, and tell the
     * interrupt driver to stop checking the data ready bit in the
     * line status register.
     */
    if (icom_port_info->flags & ASYNC_INITIALIZED) {
        cmdReg = readb(&icom_port_info->dram->CmdReg);
        writeb(cmdReg & (unsigned char)~CMD_RCV_ENABLE,&icom_port_info->dram->CmdReg);
        spin_unlock_irqrestore(&icom_lock,flags);

        /*
         * Before we drop DTR, make sure the UART transmitter
         * has completely drained; this is especially
         * important if there is a transmit FIFO!
         */
        icom_wait_until_sent(tty, icom_port_info->timeout);
        spin_lock_irqsave(&icom_lock,flags);
    }

    shutdown(icom_port_info);

    if (tty->driver.flush_buffer) {
        spin_unlock_irqrestore(&icom_lock,flags);
        tty->driver.flush_buffer(tty);
        spin_lock_irqsave(&icom_lock,flags);
    }
    spin_unlock_irqrestore(&icom_lock,flags);
    tty_ldisc_flush(tty);
    spin_lock_irqsave(&icom_lock,flags);
    tty->closing = 0;
    icom_port_info->event = 0;
    icom_port_info->tty = 0;

    if (icom_port_info->blocked_open) {
        if (icom_port_info->close_delay) {
            current->state = TASK_UNINTERRUPTIBLE;
            spin_unlock_irqrestore(&icom_lock,flags);
            schedule_timeout(icom_port_info->close_delay);
            spin_lock_irqsave(&icom_lock,flags);
       }
        wake_up_interruptible(&icom_port_info->open_wait);
    }
    icom_port_info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE|
                               ASYNC_CLOSING);

    wake_up_interruptible(&icom_port_info->close_wait);
    MOD_DEC_USE_COUNT;
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int icom_write(struct tty_struct * tty, int from_user,
                      const unsigned char * buf, int count)
{
    struct icom_port  *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned long     data_count = count;
    unsigned char     *data;
    unsigned char     cmdReg;
    unsigned long     offset;
    unsigned long     flags;


    if (!tty) {
        printk(KERN_WARNING"icom:  icom_write - no tty\n");
        return 0;
    }

    icom_port_info = (struct icom_port *)tty->driver_data;
    trace_nolock(icom_port_info, TRACE_WRITE | TRACE_TIME,jiffies);

    if (cpu_to_le16(icom_port_info->statStg->xmit[0].flags) & SA_FLAGS_READY_TO_XMIT) {
        trace_nolock(icom_port_info, TRACE_WRITE_FULL,0);
        return 0;
    }

    if (data_count > XMIT_BUFF_SZ)
        data_count = XMIT_BUFF_SZ;

    if (from_user) {
        data_count -= copy_from_user(icom_port_info->xmit_buf, buf, data_count);
        if (!data_count) {
            trace_nolock(icom_port_info, TRACE_WRITE_NODATA,0);
            return -EFAULT;
        }
    }
    else {
        memcpy(icom_port_info->xmit_buf, buf, data_count);
    }

    data = icom_port_info->xmit_buf;

    if (data_count) {
        spin_lock_irqsave(&icom_lock,flags);
        icom_port_info->statStg->xmit[0].flags = (unsigned short int)cpu_to_le16(SA_FLAGS_READY_TO_XMIT);
        icom_port_info->statStg->xmit[0].leLength = (unsigned short int)cpu_to_le16(data_count);
        offset = (unsigned long)&icom_port_info->statStg->xmit[0] - (unsigned long)icom_port_info->statStg;
        *icom_port_info->xmitRestart = cpu_to_le32(icom_port_info->statStg_pci + offset);
        cmdReg = readb(&icom_port_info->dram->CmdReg);
        writeb(cmdReg | CMD_XMIT_RCV_ENABLE,&icom_port_info->dram->CmdReg);
        writeb(START_XMIT,&icom_port_info->dram->StartXmitCmd);
        trace_lock(icom_port_info, TRACE_WRITE_START,data_count);
        spin_unlock_irqrestore(&icom_lock,flags);
    }

    return data_count;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_put_char(struct tty_struct * tty, unsigned char ch)
{
    /* icom_put_char adds the character to the current buffer, the
     * data is not actually sent until icom_flush_chars is called.
     * Per definition icom_flush_chars MUST be called after
     * icom_put_char
     */

    unsigned char     *data;
    struct icom_port  *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned long      flags;

    trace_nolock(icom_port_info, TRACE_PUT_CHAR, ch);

    if (cpu_to_le16(icom_port_info->statStg->xmit[0].flags) & SA_FLAGS_READY_TO_XMIT) {
        trace_nolock(icom_port_info, TRACE_PUT_FULL,0);
        return;
    }

    spin_lock_irqsave(&icom_lock,flags);
    data = icom_port_info->xmit_buf;
    data[icom_port_info->put_length] = ch;

    if (!tty->stopped && !tty->hw_stopped) {
        icom_port_info->put_length++;
    }

    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_flush_chars(struct tty_struct * tty)
{
    struct icom_port  *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned char     cmdReg;
    unsigned long     offset;
    unsigned long     flags;

    trace_nolock(icom_port_info, TRACE_FLUSH_CHAR | TRACE_TIME,jiffies);

    spin_lock_irqsave(&icom_lock,flags);
    if (icom_port_info->put_length) {
        trace_lock(icom_port_info, TRACE_START_FLUSH,icom_port_info->put_length);
        icom_port_info->statStg->xmit[0].flags = (unsigned short int)cpu_to_le16(SA_FLAGS_READY_TO_XMIT);
        icom_port_info->statStg->xmit[0].leLength = (unsigned short int)cpu_to_le16(icom_port_info->put_length);
        offset = (unsigned long)&icom_port_info->statStg->xmit[0] - (unsigned long)icom_port_info->statStg;
        *icom_port_info->xmitRestart = cpu_to_le32(icom_port_info->statStg_pci + offset);
        cmdReg = readb(&icom_port_info->dram->CmdReg);
        writeb(cmdReg | CMD_XMIT_RCV_ENABLE,&icom_port_info->dram->CmdReg);
        writeb(START_XMIT,&icom_port_info->dram->StartXmitCmd);

        icom_port_info->put_length = 0;
    }
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int icom_write_room(struct tty_struct * tty)
{
    int bytes_avail;
    struct icom_port *icom_port_info = tty->driver_data;

    if (cpu_to_le16(icom_port_info->statStg->xmit[0].flags) & SA_FLAGS_READY_TO_XMIT)
        bytes_avail = 0;
    else
        bytes_avail = XMIT_BUFF_SZ;

    trace_nolock(icom_port_info, TRACE_WRITE_ROOM,bytes_avail);
    return bytes_avail;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int icom_chars_in_buffer(struct tty_struct * tty)
{
    unsigned long dram;
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    int number_remaining = 0;

    trace_nolock(icom_port_info, TRACE_CHARS_IN_BUFF,0);
    if (cpu_to_le16(icom_port_info->statStg->xmit[0].flags) & SA_FLAGS_READY_TO_XMIT) {
        dram = (unsigned long)icom_port_info->dram;
        number_remaining = readw((void *)(dram + 0x168));
        trace_nolock(icom_port_info, TRACE_CHARS_REMAIN,number_remaining);
    }
    return number_remaining;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int get_modem_info(struct icom_port * icom_port_info, unsigned int *value)
{
    unsigned char status,control;
    unsigned int result;

    trace_nolock(icom_port_info, TRACE_GET_MODEM,0);

    status = readb(&icom_port_info->dram->isr);
    control = readb(&icom_port_info->dram->osr);

    result =  ((control & 0x40) ? TIOCM_RTS : 0)
        | ((control & ICOM_DTR) ? TIOCM_DTR : 0)
        | ((status  & ICOM_DCD) ? TIOCM_CAR : 0)
        | ((status  & ICOM_RI ) ? TIOCM_RNG : 0)
        | ((status  & ICOM_DSR) ? TIOCM_DSR : 0)
        | ((status  & ICOM_CTS) ? TIOCM_CTS : 0);
    return put_user(result,value);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int set_modem_info(struct icom_port * icom_port_info, unsigned int cmd,
                          unsigned int *value)
{
    int error;
    unsigned int arg;
    unsigned char local_osr;

    trace_nolock(icom_port_info, TRACE_SET_MODEM,0);
    local_osr = readb(&icom_port_info->dram->osr);

    error = get_user(arg, value);
    if (error)
        return error;
    switch (cmd) {
        case TIOCMBIS: 
            if (arg & TIOCM_RTS) {
                trace_nolock(icom_port_info, TRACE_RAISE_RTS,0);
                local_osr |= ICOM_RTS;
            }
            if (arg & TIOCM_DTR) {
                trace_nolock(icom_port_info, TRACE_RAISE_DTR,0);
                local_osr |= ICOM_DTR;
            }
            break;
        case TIOCMBIC:
            if (arg & TIOCM_RTS) {
                trace_nolock(icom_port_info, TRACE_LOWER_RTS,0);
                local_osr &= ~ICOM_RTS;
            }
            if (arg & TIOCM_DTR) {
                trace_nolock(icom_port_info, TRACE_LOWER_DTR,0);
                local_osr &= ~ICOM_DTR;
            }
            break;
        case TIOCMSET:
            local_osr = ((local_osr & ~(ICOM_RTS | ICOM_DTR))
                         | ((arg & TIOCM_RTS) ? ICOM_RTS : 0)
                         | ((arg & TIOCM_DTR) ? ICOM_DTR : 0));
            break;
        default:
            return -EINVAL;
    }

    writeb(local_osr,&icom_port_info->dram->osr);
    return 0;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int get_serial_info(struct icom_port * icom_port_info,
                           struct serial_struct * retinfo)
{
    struct serial_struct tmp;

    trace_nolock(icom_port_info, TRACE_GET_SERIAL,0);

    if (!retinfo)
        return -EFAULT;
    memset(&tmp, 0, sizeof(tmp));
    tmp.type = 0x00; /* device specific, PORT_UNKNOWN */
    tmp.line = icom_port_info->adapter; /* adapter number */
    tmp.port = icom_port_info->port; /* port number on adapter */
    tmp.irq = icom_adapter_info[icom_port_info->adapter].irq_number;
    tmp.flags = icom_port_info->flags;
    tmp.xmit_fifo_size = XMIT_BUFF_SZ;
    tmp.baud_base = 0x00; /* device specific */
    tmp.close_delay = icom_port_info->close_delay;
    tmp.closing_wait = icom_port_info->closing_wait;
    tmp.custom_divisor = 0x00;  /* device specific */
    tmp.hub6 = 0x00; /* device specific */
    if (copy_to_user(retinfo,&tmp,sizeof(*retinfo)))
        return -EFAULT;
    return 0;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int set_serial_info(struct icom_port * icom_port_info,
                           struct serial_struct * new_info)
{
    struct serial_struct new_serial;
    int                  old_flags;
    int 		       retval = 0;
    unsigned long        flags;

    trace_nolock(icom_port_info, TRACE_SET_SERIAL,0);

    if (copy_from_user(&new_serial,new_info,sizeof(new_serial)))
        return -EFAULT;

    spin_lock_irqsave(&icom_lock,flags);

    old_flags = icom_port_info->flags;
    /* new_serial.irq --- irq of adapter will not change, PCI only */
    /* new_serial.xmit_fifo_size -- can not change on this device */
    /* new_serial.baud_base -- ??? */
    /* new_serial.custom_divisor -- device specific */
    /* new_serial.hub6 -- device specific */
    /* new_serial.type -- device specific */
    /* new_serial.port -- address of port will not change, PCI only */

    if (!capable(CAP_SYS_ADMIN)) {
        if ((new_serial.baud_base != icom_port_info->baud_base) ||
            (new_serial.close_delay != icom_port_info->close_delay) ||
            ((new_serial.flags & ~ASYNC_USR_MASK) !=
             (icom_port_info->flags & ~ASYNC_USR_MASK))) {

            spin_unlock_irqrestore(&icom_lock,flags);
            return -EPERM;
        }

        icom_port_info->flags = ((icom_port_info->flags & ~ASYNC_USR_MASK) |
                                 (new_serial.flags & ASYNC_USR_MASK));
        goto check_and_exit;
    }

    if (new_serial.baud_base < 9600) {
        spin_unlock_irqrestore(&icom_lock,flags);
        return -EINVAL;
    }

    /*
     * OK, past this point, all the error checking has been done.
     * At this point, we start making changes.....
     */
    icom_port_info->baud_base = new_serial.baud_base;
    icom_port_info->flags = ((icom_port_info->flags & ~ASYNC_FLAGS) |
                             (new_serial.flags & ASYNC_FLAGS));
    icom_port_info->close_delay = new_serial.close_delay * HZ/100;
    icom_port_info->closing_wait = new_serial.closing_wait * HZ/100;
    icom_port_info->tty->low_latency = (icom_port_info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

    check_and_exit:

    if (icom_port_info->flags & ASYNC_INITIALIZED) {
        if (((icom_port_info->flags & ASYNC_SPD_MASK) !=
             (old_flags & ASYNC_SPD_MASK))) {
            if ((icom_port_info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
                icom_port_info->tty->alt_speed = 57600;
            if ((icom_port_info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
                icom_port_info->tty->alt_speed = 115200;
            if ((icom_port_info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
                icom_port_info->tty->alt_speed = 230400;
            if ((icom_port_info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
                icom_port_info->tty->alt_speed = 460800;
            change_speed(icom_port_info, 0);
        }
    }
    else
        retval = startup(icom_port_info);

    spin_unlock_irqrestore(&icom_lock,flags);
    return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space. 
 *
 * Context:  Task Level
 * Locks:    none
 */
static int get_lsr_info(struct icom_port * info, unsigned int *value)
{
    unsigned char status;
    unsigned int result;

    trace_nolock(info, TRACE_SET_LSR,0);

    status = cpu_to_le16(info->statStg->xmit[0].flags);
    result = ((status & SA_FLAGS_DONE) ? TIOCSER_TEMT : 0);
    return put_user(result,value);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int icom_ioctl(struct tty_struct * tty, struct file * filp,
                      unsigned int cmd, unsigned long arg) 
{
    DECLARE_WAITQUEUE(wait,current);
    int                            error;
    struct icom_port              *icom_port_info = (struct icom_port *)tty->driver_data;
    struct async_icount            cprev, cnow; /* kernel counter temps */
    struct serial_icounter_struct *p_cuser;	/* user space */
    unsigned long                  flags;
    unsigned int                   result;

    trace_nolock(icom_port_info, TRACE_IOCTL | TRACE_TIME,jiffies);
    if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
        (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
        (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
        if (tty->flags & (1 << TTY_IO_ERROR))
            return -EIO;
    }

    switch (cmd) {
        case 0x4300:
            if (copy_to_user((void *)arg,icom_port_info->trace_blk,TRACE_BLK_SZ))
                return -EFAULT;
            return 0;
        case TIOCMGET:
            return get_modem_info(icom_port_info, (unsigned int *) arg);
        case TIOCMBIS:
        case TIOCMBIC:
        case TIOCMSET:
            return set_modem_info(icom_port_info, cmd, (unsigned int *) arg);
        case TIOCGSERIAL:
            return get_serial_info(icom_port_info,
                                   (struct serial_struct *) arg);
        case TIOCSSERIAL:
            return set_serial_info(icom_port_info,
                                   (struct serial_struct *) arg);

        case TIOCSERGETLSR: /* Get line status register */
            return get_lsr_info(icom_port_info, (unsigned int *) arg);

            /*
             * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
             * - mask passed in arg for lines of interest
             *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
             * Caller should use TIOCGICOUNT to see which one it was
             */
        case TIOCMIWAIT:
            add_wait_queue(&icom_port_info->delta_msr_wait, &wait);

            /* note the counters on entry */
            spin_lock_irqsave(&icom_lock,flags);
            cprev = icom_port_info->icount;
            current->state = TASK_INTERRUPTIBLE;
            spin_unlock_irqrestore(&icom_lock,flags);

            while (1) {
                schedule();

                /* see if a signal did it */
                if (signal_pending(current)) {
                    result = -ERESTARTSYS;
                    break;
                }

                spin_lock_irqsave(&icom_lock,flags);
                cnow = icom_port_info->icount;
                current->state = TASK_INTERRUPTIBLE;
                spin_unlock_irqrestore(&icom_lock,flags);

                if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr && 
                    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts) {
                    result = -EIO; /* no change => error */
                    break;
                }
                if ( ((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
                     ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
                     ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
                     ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
                    result = 0;
                    break;
                }
                cprev = cnow;
            }

            current->state = TASK_RUNNING;
            remove_wait_queue(&icom_port_info->delta_msr_wait, &wait);

            return result;

            /* 
             * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
             * Return: write counters to the user passed counter struct
             * NB: both 1->0 and 0->1 transitions are counted except for
             *     RI where only 0->1 is counted.
             */
        case TIOCGICOUNT:
            spin_lock_irqsave(&icom_lock,flags);
            cnow = icom_port_info->icount;
            spin_unlock_irqrestore(&icom_lock,flags);

            p_cuser = (struct serial_icounter_struct *) arg;
            error = put_user(cnow.cts, &p_cuser->cts);
            if (error) return error;
            error = put_user(cnow.dsr, &p_cuser->dsr);
            if (error) return error;
            error = put_user(cnow.rng, &p_cuser->rng);
            if (error) return error;
            error = put_user(cnow.dcd, &p_cuser->dcd);
            if (error) return error;
            error = put_user(cnow.rx, &p_cuser->rx);
            if (error) return error;
            error = put_user(cnow.tx, &p_cuser->tx);
            if (error) return error;
            error = put_user(cnow.frame, &p_cuser->frame);
            if (error) return error;
            error = put_user(cnow.overrun, &p_cuser->overrun);
            if (error) return error;
            error = put_user(cnow.parity, &p_cuser->parity);
            if (error) return error;
            error = put_user(cnow.brk, &p_cuser->brk);
            if (error) return error;
            error = put_user(cnow.buf_overrun, &p_cuser->buf_overrun);
            if (error) return error;			
            return 0;

        case TIOCSERGWILD:
        case TIOCSERSWILD:
            /* "setserial -W" is called in Debian boot */
            printk (KERN_INFO"TIOCSER?WILD ioctl obsolete, ignored.\n");
            return 0;

        default:
            trace_nolock(icom_port_info, TRACE_IOCTL_IGNORE,cmd);
            return -ENOIOCTLCMD;
    }
    return 0;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_send_xchar(struct tty_struct * tty, char ch)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned char    xdata;
    int              index;
    unsigned long    flags;

    trace_nolock(icom_port_info, TRACE_SEND_XCHAR,ch);
    /* attempt sending char for a period of .1 second */
    for (index = 0; index < 10; index++ ) {
        spin_lock_irqsave(&icom_lock,flags);
        xdata = readb(&icom_port_info->dram->xchar);
        if (xdata == 0x00) {
            trace_lock(icom_port_info, TRACE_QUICK_WRITE,0);
            writeb(ch,&icom_port_info->dram->xchar);
            spin_unlock_irqrestore(&icom_lock,flags);
            break;
        }
        spin_unlock_irqrestore(&icom_lock,flags);
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/100);
    }
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_throttle(struct tty_struct * tty)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned char    osr;
    unsigned long    flags;

    trace_nolock(icom_port_info, TRACE_THROTTLE,0);
    if (I_IXOFF(tty))
        icom_send_xchar(tty, STOP_CHAR(tty));

    if (tty->termios->c_cflag & CRTSCTS) {
        spin_lock_irqsave(&icom_lock,flags);
        osr = readb(&icom_port_info->dram->osr);
        writeb(osr & ~ICOM_RTS,&icom_port_info->dram->osr);
        spin_unlock_irqrestore(&icom_lock,flags);
    }
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_unthrottle(struct tty_struct * tty)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned char    osr;
    unsigned long    flags;

    trace_nolock(icom_port_info, TRACE_UNTHROTTLE,0);
    if (I_IXOFF(tty)) {
        icom_send_xchar(tty, START_CHAR(tty));
    }
    if (tty->termios->c_cflag & CRTSCTS) {
        spin_lock_irqsave(&icom_lock,flags);
        osr = readb(&icom_port_info->dram->osr);
        writeb(osr | ICOM_RTS,&icom_port_info->dram->osr);
        spin_unlock_irqrestore(&icom_lock,flags);
    }
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_set_termios(struct tty_struct * tty, struct termios * old_termios)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned int     cflag = tty->termios->c_cflag;
    unsigned char    osr;
    unsigned long    flags;
#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

    spin_lock_irqsave(&icom_lock,flags);
    trace_lock(icom_port_info, TRACE_SET_TERMIOS,0);
    if ((cflag == old_termios->c_cflag)
        && (RELEVANT_IFLAG(tty->termios->c_iflag) 
            == RELEVANT_IFLAG(old_termios->c_iflag))) {
        spin_unlock_irqrestore(&icom_lock,flags);
        return;
    }

    change_speed(icom_port_info, old_termios);

    /* Handle transition to B0 status */
    if ((old_termios->c_cflag & CBAUD) &&
        !(cflag & CBAUD)) {
        trace_lock(icom_port_info, TRACE_DROP_DTR_RTS,0);

        osr = readb(&icom_port_info->dram->osr);
        writeb(osr & ~(ICOM_DTR|ICOM_RTS),&icom_port_info->dram->osr);
    }

    /* Handle transition away from B0 status */
    if (!(old_termios->c_cflag & CBAUD) &&
        (cflag & CBAUD)) {
        trace_lock(icom_port_info, TRACE_RAISE_DTR,0);

        osr = readb(&icom_port_info->dram->osr);
        osr |= ICOM_DTR;
        if (!(tty->termios->c_cflag & CRTSCTS) || 
            !test_bit(TTY_THROTTLED, &tty->flags)) {
            trace_lock(icom_port_info, TRACE_RAISE_RTS,0);
            osr |= ICOM_RTS;
        }
        writeb(osr,&icom_port_info->dram->osr);
    }

    /* Handle turning off CRTSCTS */
    if ((old_termios->c_cflag & CRTSCTS) &&
        !(tty->termios->c_cflag & CRTSCTS)) {
        tty->hw_stopped = 0;
        spin_unlock_irqrestore(&icom_lock,flags);
        icom_start(tty);
        spin_lock_irqsave(&icom_lock,flags);
    }

#if 0
    /*
     * No need to wake up processes in open wait, since they
     * sample the CLOCAL flag once, and don't recheck it.
     * XXX  It's not clear whether the current behavior is correct
     * or not.  Hence, this may change.....
     */
    if (!(old_termios->c_cflag & CLOCAL) &&
        (tty->termios->c_cflag & CLOCAL))
        wake_up_interruptible(&icom_port_info->open_wait);
#endif
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_stop(struct tty_struct * tty)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned char    cmdReg;
    unsigned long    flags;

    spin_lock_irqsave(&icom_lock,flags);
    trace_lock(icom_port_info, TRACE_STOP,0);
    cmdReg = readb(&icom_port_info->dram->CmdReg);
    writeb(cmdReg | CMD_HOLD_XMIT,&icom_port_info->dram->CmdReg);
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_start(struct tty_struct * tty)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned char    cmdReg;
    unsigned long    flags;

    spin_lock_irqsave(&icom_lock,flags);
    trace_lock(icom_port_info, TRACE_START,0);
    cmdReg = readb(&icom_port_info->dram->CmdReg);
    writeb(cmdReg & ~CMD_HOLD_XMIT,&icom_port_info->dram->CmdReg);
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_hangup(struct tty_struct * tty)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned long    flags;

    trace_nolock(icom_port_info, TRACE_HANGUP,0);
    icom_flush_buffer(tty);
    spin_lock_irqsave(&icom_lock,flags);
    shutdown(icom_port_info);
    icom_port_info->open_active_count = 0;
    icom_port_info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
    icom_port_info->tty = 0;
    wake_up_interruptible(&icom_port_info->open_wait);
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_break(struct tty_struct *tty, int break_state)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned char    cmdReg;
    unsigned long    flags;

    spin_lock_irqsave(&icom_lock,flags);
    trace_lock(icom_port_info, TRACE_BREAK,0);
    cmdReg = readb(&icom_port_info->dram->CmdReg);
    if (break_state == -1) {
        writeb(cmdReg | CMD_SND_BREAK,&icom_port_info->dram->CmdReg);
    }
    else {
        writeb(cmdReg & ~CMD_SND_BREAK,&icom_port_info->dram->CmdReg);
    }
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * icom_wait_until_sent() --- wait until the transmitter is empty
 *
 * Context:  Task Level
 * Locks:    none
 */
static void icom_wait_until_sent(struct tty_struct *tty, int timeout)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned long orig_jiffies, char_time;
    int status;

    trace_nolock(icom_port_info, TRACE_WAIT_UNTIL_SENT,0);

    orig_jiffies = jiffies;
    /*
     * Set the check interval to be 1/5 of the estimated time to
     * send a single character, and make it at least 1.  The check
     * interval should also be less than the timeout.
     * 
     * Note: we have to use pretty tight timings here to satisfy
     * the NIST-PCTS.
     */
    char_time = (icom_port_info->timeout - HZ/50) / icom_port_info->xmit_fifo_size;
    char_time = char_time / 5;
    if (char_time == 0)
        char_time = 1;
    if (timeout) {
        if (timeout < char_time)
            char_time = timeout;
    }
    /*
     * If the transmitter hasn't cleared in twice the approximate
     * amount of time to send the entire FIFO, it probably won't
     * ever clear.  This assumes the UART isn't doing flow
     * control, which is currently the case.  Hence, if it ever
     * takes longer than icom_port_info->timeout, this is probably due to a
     * UART bug of some kind.  So, we clamp the timeout parameter at
     * 2*icom_port_info->timeout.
     */
    if (!timeout || timeout > 2*icom_port_info->timeout)
        timeout = 2*icom_port_info->timeout;

    status = cpu_to_le16(icom_port_info->statStg->xmit[0].flags);
    while (status & SA_FLAGS_DONE ) {  /*data still transmitting*/

        current->state = TASK_UNINTERRUPTIBLE;
        /* XXX FIXME
         current->counter = 0;	*/ /* make us low-priority */
        schedule_timeout(char_time);
        if (signal_pending(current))
            break;
        if (timeout && time_after(jiffies, orig_jiffies + timeout))
            break;
        status = cpu_to_le16(icom_port_info->statStg->xmit[0].flags);
    }
    current->state = TASK_RUNNING;
}

/*
 * /proc fs routines....
 *
 * Context:  Task Level
 * Locks:    none
 */
static inline int line_info(char *buf, int free_space, struct icom_port *icom_port_info)
{
    char	line[72], control, status;
    int	ret, baud_index, len;
    int     port;

    if ((icom_port_info->port == 2) &&
        (icom_adapter_info[icom_port_info->adapter].subsystem_id != FOUR_PORT_MODEL))
        port = 1;
    else
        port = icom_port_info->port;

    memset(line, 0,sizeof(line));
    len = sprintf(line,"   port:%d      maj:%d min:%d cable:",
                  port,
                  243,
                  icom_port_info->minor_number);
    if (!icom_port_info->passed_diags) {
        len += sprintf(line+len," PortFailed\n");
    }
    else if (icom_port_info->imbed_modem == ICOM_IMBED_MODEM) {
        len += sprintf(line+len," InternalModem\n");
    }
    else if (icom_port_info->cable_id == RS232_CABLE) {
        len += sprintf(line+len," RS232\n");
    }
    else {
        len += sprintf(line+len," ----\n");
    }

    if (len > free_space) return 0;
    ret = sprintf(buf, "%s", line);

    memset(line, 0,sizeof(line));
    baud_index = readb(&icom_port_info->dram->async_config3);
    len = sprintf(line, "     baud:%d",icom_acfg_baud[baud_index]);

    len += sprintf(line+len, " tx:%d rx:%d",
                   icom_port_info->icount.tx, icom_port_info->icount.rx);

    if (icom_port_info->icount.frame)
        len += sprintf(line+len, " fe:%d", icom_port_info->icount.frame);

    if (icom_port_info->icount.parity)
        len += sprintf(line+len, " pe:%d", icom_port_info->icount.parity);

    if (icom_port_info->icount.brk)
        len += sprintf(line+len, " brk:%d", icom_port_info->icount.brk);	

    if (icom_port_info->icount.overrun)
        len += sprintf(line+len, " oe:%d", icom_port_info->icount.overrun);

    if ((ret + len) > free_space) return 0;
    ret += sprintf(buf+ret, "%s", line);

    /*
     * Last thing is the RS-232 status lines
     */
    memset(line, 0,sizeof(line));
    status = readb(&icom_port_info->dram->isr);
    control = readb(&icom_port_info->dram->osr);

    line[0] = 0;
    line[1] = 0;
    len = 0;
    if (control & ICOM_RTS)
        len += sprintf(line+len, "|RTS");
    if (status & ICOM_CTS)
        len += sprintf(line+len, "|CTS");
    if (control & ICOM_DTR)
        len += sprintf(line+len, "|DTR");
    if (status & ICOM_DSR)
        len += sprintf(line+len, "|DSR");
    if (status & ICOM_DCD)
        len += sprintf(line+len, "|CD");
    if (status & ICOM_RI)
        len += sprintf(line+len, "|RI");

    if ((ret + len) > free_space) return 0;
    ret += sprintf(buf+ret, "   %s\n", line+1);

    return ret;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int icom_read_proc(char *page, char **start, off_t off, int count,
                   int *eof, void *data)
{
    int i, j, len = 0, l;
    off_t	begin = 0;
    char line[72];
    struct LocationDataStruct *p_location_data = NULL;

    len += sprintf(page, "%s: %s\n",ICOM_DRIVER_NAME, ICOM_VERSION_STR);
    for (i = 0; i < active_adapters; i++) {
        memset(line, 0,sizeof(line));
#ifdef CONFIG_PPC_ISERIES
        p_location_data = iSeries_GetLocationData(icom_adapter_info[i].pci_dev);

        if (p_location_data != NULL) {
            l = sprintf(line,"Frame ID: %d", p_location_data->FrameId);
            l += sprintf(line+l," Card Position: %s", p_location_data->CardLocation);
            kfree(p_location_data);
        }
        else
            l = sprintf(line, "Adapter %d", i);
#else
        l = sprintf(line, "Adapter %d", i);
#endif
        switch (icom_adapter_info[i].subsystem_id) {
            case FOUR_PORT_MODEL:
                l += sprintf(line+l," CCIN: 2805\n");
                break;
            case V2_TWO_PORTS_RVX:
                l += sprintf(line+l," CCIN: 2742\n");
                break;
            case V2_ONE_PORT_RVX_ONE_PORT_IMBED_MDM:
                l += sprintf(line+l," CCIN: 2793\n");
                break;
            default:
                l += sprintf(line+l, "\n");
        }

        if ((l + len) < count) {
            len += sprintf(page + len,"%s",line);
            if (len+begin <= off) {
                begin += len;
                len = 0;
            }
        }
        else
            goto done;

        for (j= 0; j < 4; j++) {
            if (icom_adapter_info[i].port_info[j].status == ICOM_PORT_ACTIVE) {
                l = line_info(page + len, count - len, &icom_adapter_info[i].port_info[j]);
                /* l == 0 means line_info could not fit new data into remaining space */
                if (l == 0)
                    goto done;
                len += l;
                if (len+begin <= off) {
                    begin += len;
                    len = 0;
                }
            }
        }
    }
    *eof = 1;
    done:
        if (off >= len+begin)
            return 0;
    /*
     if returning multiple pages, the time delta between sending pages
     may experience changes in the data size represented in the pages
     already sent.  The logic as is may duplicate a line but should
     never miss a line
     */
    *start = page;
    return len;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void icom_flush_buffer(struct tty_struct * tty)
{
    struct icom_port *icom_port_info = (struct icom_port *)tty->driver_data;
    unsigned char    cmdReg;
    unsigned long    flags;

    spin_lock_irqsave(&icom_lock,flags);
    trace_lock(icom_port_info, TRACE_FLUSH_BUFFER,0);
    /*
     * with no CMD_XMIT_ENABLE is same as disabling xmitter.  This should
     * result in an interrupt if currently transmitting
     */
    cmdReg = readb(&icom_port_info->dram->CmdReg);
    writeb(cmdReg & ~CMD_XMIT_ENABLE,&icom_port_info->dram->CmdReg);  
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 *
 * Context:  Interrupt Level
 * Locks:    none
 */
static inline void rs_sched_event(struct icom_port *info,
                                  int event)
{
    info->event |= 1 << event;
    queue_task(&info->tqueue, &tq_immediate);
    mark_bh(IMMEDIATE_BH);
}

/*
 * Context:  Interrupt Level
 * Locks:    icom_lock
 */
static inline void check_modem_status(struct icom_port *icom_port_info)
{
    static char old_status = 0;
    char delta_status;
    unsigned char status;

    /*modem input register */
    status = readb(&icom_port_info->dram->isr);
    trace_lock(icom_port_info, TRACE_CHECK_MODEM,status);
    delta_status = status ^ old_status;
    if (delta_status) {
        if (delta_status & ICOM_RI)
            icom_port_info->icount.rng++;
        if (delta_status & ICOM_DSR)
            icom_port_info->icount.dsr++;
        if (delta_status & ICOM_DCD)
            icom_port_info->icount.dcd++;
        if (delta_status & ICOM_CTS)
            icom_port_info->icount.cts++;

        wake_up_interruptible(&icom_port_info->delta_msr_wait);
        old_status = status;
    }

    if ((icom_port_info->flags & ASYNC_CHECK_CD) && (status & ICOM_DCD)) {
        if (status & ICOM_DCD) /* Carrier Detect up */
            wake_up_interruptible(&icom_port_info->open_wait);
        else if (!((icom_port_info->flags & ASYNC_CALLOUT_ACTIVE) &&
                   (icom_port_info->flags & ASYNC_CALLOUT_NOHUP))) {
            if (icom_port_info->tty)
                tty_hangup(icom_port_info->tty);
        }
    }

    if (icom_port_info->flags & ASYNC_CTS_FLOW) {
        if (icom_port_info->tty->hw_stopped) {
            if (status & ICOM_CTS) {  /* CTS up */
                icom_port_info->tty->hw_stopped = 0;
                trace_lock(icom_port_info, TRACE_CTS_UP,0);
                rs_sched_event(icom_port_info, 0);
                return;
            }
        }
        else {
            if (!(status & ICOM_CTS)) { /* CTS down */
                icom_port_info->tty->hw_stopped = 1;
                trace_lock(icom_port_info, TRACE_CTS_DOWN,0);
            }
        }
    }
}

/*
 * Context:  Interrupt Level
 * Locks:    icom_lock
 */
static void process_interrupt(u16 port_int_reg, struct icom_port *icom_port_info)
{
    short int           count, rcv_buff;
    struct tty_struct   *tty = icom_port_info->tty;
    unsigned char       *data;
    unsigned short int  status;
    struct async_icount *icount;
    unsigned long        offset;


    trace_lock(icom_port_info, TRACE_INTERRUPT | TRACE_TIME,jiffies);

    if (port_int_reg & (INT_XMIT_COMPLETED | INT_XMIT_DISABLED)) {
        if (port_int_reg & (INT_XMIT_COMPLETED))
            trace_lock(icom_port_info, TRACE_XMIT_COMPLETE,0);
        else
            trace_lock(icom_port_info, TRACE_XMIT_DISABLED,0);

        /* clear buffer in use bit */
        icom_port_info->statStg->xmit[0].flags &= cpu_to_le16(~SA_FLAGS_READY_TO_XMIT);
        icom_port_info->icount.tx += (unsigned short int)cpu_to_le16(icom_port_info->statStg->xmit[0].leLength);

        /* activate write queue */
        rs_sched_event(icom_port_info, 0);
    }

    if (port_int_reg & INT_RCV_COMPLETED) {

        trace_lock(icom_port_info, TRACE_RCV_COMPLETE,0);
        rcv_buff = icom_port_info->next_rcv;

        status = cpu_to_le16(icom_port_info->statStg->rcv[rcv_buff].flags);
        while (status & SA_FL_RCV_DONE) {

            trace_lock(icom_port_info, TRACE_FID_STATUS,status);

            count = cpu_to_le16(icom_port_info->statStg->rcv[rcv_buff].leLength);

            trace_lock(icom_port_info, TRACE_RCV_COUNT,count);
            if (count > (TTY_FLIPBUF_SIZE - tty->flip.count))
                count = TTY_FLIPBUF_SIZE - tty->flip.count;

            trace_lock(icom_port_info, TRACE_REAL_COUNT,count);

            offset = cpu_to_le32(icom_port_info->statStg->rcv[rcv_buff].leBuffer) - icom_port_info->recv_buf_pci;

            memcpy(tty->flip.char_buf_ptr,(unsigned char *)((unsigned long)icom_port_info->recv_buf + offset),count);

            data = (unsigned char *)tty->flip.char_buf_ptr;

            if (count > 0) {
                tty->flip.count += count - 1;
                tty->flip.char_buf_ptr += count - 1;

                memset(tty->flip.flag_buf_ptr, 0, count);
                tty->flip.flag_buf_ptr += count - 1;
            }

            icount = &icom_port_info->icount;
            icount->rx += count;

            /* Break detect logic */
            if ((status & SA_FLAGS_FRAME_ERROR) && (tty->flip.char_buf_ptr[0] == 0x00)) {
                status &= ~SA_FLAGS_FRAME_ERROR;
                status |= SA_FLAGS_BREAK_DET;
                trace_lock(icom_port_info, TRACE_BREAK_DET,0);
            }

            if (status & (SA_FLAGS_BREAK_DET | SA_FLAGS_PARITY_ERROR |
                          SA_FLAGS_FRAME_ERROR | SA_FLAGS_OVERRUN)) {

                if (status & SA_FLAGS_BREAK_DET)
                    icount->brk++;
                if (status & SA_FLAGS_PARITY_ERROR)
                    icount->parity++;
                if (status & SA_FLAGS_FRAME_ERROR)
                    icount->frame++;
                if (status & SA_FLAGS_OVERRUN)
                    icount->overrun++;

                /*
                 * Now check to see if character should be
                 * ignored, and mask off conditions which
                 * should be ignored.
                 */ 
                if (status & icom_port_info->ignore_status_mask) {
                    trace_lock(icom_port_info, TRACE_IGNORE_CHAR,0);
                    goto ignore_char;
                }

                status &= icom_port_info->read_status_mask;

                if (status & SA_FLAGS_BREAK_DET) {
                    *tty->flip.flag_buf_ptr = TTY_BREAK;
                    if (icom_port_info->flags & ASYNC_SAK)
                        do_SAK(tty);
                }
                else if (status & SA_FLAGS_PARITY_ERROR) {
                    trace_lock(icom_port_info, TRACE_PARITY_ERROR,0);
                    *tty->flip.flag_buf_ptr = TTY_PARITY;
                }
                else if (status & SA_FLAGS_FRAME_ERROR)
                    *tty->flip.flag_buf_ptr = TTY_FRAME;
                if (status & SA_FLAGS_OVERRUN) {
                    /*
                     * Overrun is special, since it's
                     * reported immediately, and doesn't
                     * affect the current character
                     */
                    if (tty->flip.count < TTY_FLIPBUF_SIZE) {
                        tty->flip.count++;
                        tty->flip.flag_buf_ptr++;
                        tty->flip.char_buf_ptr++;
                        *tty->flip.flag_buf_ptr = TTY_OVERRUN;
                    }
                }
            }

            tty->flip.flag_buf_ptr++;
            tty->flip.char_buf_ptr++;
            tty->flip.count++;
            ignore_char:
                icom_port_info->statStg->rcv[rcv_buff].flags = 0;
            icom_port_info->statStg->rcv[rcv_buff].leLength = 0;
            icom_port_info->statStg->rcv[rcv_buff].WorkingLength = (unsigned short int)cpu_to_le16(RCV_BUFF_SZ);

            rcv_buff++;
            if (rcv_buff == NUM_RBUFFS) rcv_buff = 0;

            status = cpu_to_le16(icom_port_info->statStg->rcv[rcv_buff].flags);
        }
        icom_port_info->next_rcv = rcv_buff;
        tty_flip_buffer_push(tty);
    }
}

/*
 * Context:  Interrupt Level
 * Locks:    none
 */
static void icom_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
    unsigned long       int_reg;
    u32                 adapter_interrupts;
    u16                 port_int_reg;
    struct icom_adapter *icom_adapter_ptr;
    struct icom_port    *icom_port_info;
    unsigned long       flags;

    spin_lock_irqsave(&icom_lock,flags);

    /* find icom_port_info for this interrupt */
    icom_adapter_ptr = (struct icom_adapter *)dev_id;

    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        int_reg = icom_adapter_ptr->base_addr + 0x8024;

        adapter_interrupts = readl((void *)int_reg);

        if (adapter_interrupts & 0x00003FFF) {
            /* port 2 interrupt,  NOTE:  for all ADAPTER_V2, port 2 will be active */
            icom_port_info = &icom_adapter_ptr->port_info[2];
            port_int_reg = (u16)adapter_interrupts;
            process_interrupt(port_int_reg, icom_port_info);
            check_modem_status(icom_port_info);
        }
        if (adapter_interrupts & 0x3FFF0000) {
            /* port 3 interrupt */
            icom_port_info = &icom_adapter_ptr->port_info[3];
            if (icom_port_info->status == ICOM_PORT_ACTIVE) {
                port_int_reg = (u16)(adapter_interrupts >> 16);
                process_interrupt(port_int_reg, icom_port_info);
                check_modem_status(icom_port_info);
            }
        }

        /* Clear out any pending interrupts */
        writel(adapter_interrupts,(void *)int_reg);

        int_reg = icom_adapter_ptr->base_addr + 0x8004;
    }
    else {
        int_reg = icom_adapter_ptr->base_addr + 0x4004;
    }

    adapter_interrupts = readl((void *)int_reg);

    if (adapter_interrupts & 0x00003FFF) {
        /* port 0 interrupt, NOTE:  for all adapters, port 0 will be active */
        icom_port_info = &icom_adapter_ptr->port_info[0];
        port_int_reg = (u16)adapter_interrupts;
        process_interrupt(port_int_reg, icom_port_info);
        check_modem_status(icom_port_info);
    }
    if (adapter_interrupts & 0x3FFF0000) {
        /* port 1 interrupt */
        icom_port_info = &icom_adapter_ptr->port_info[1];
        if (icom_port_info->status == ICOM_PORT_ACTIVE) {
            port_int_reg = (u16)(adapter_interrupts >> 16);
            process_interrupt(port_int_reg, icom_port_info);
            check_modem_status(icom_port_info);
        }
    }

    /* Clear out any pending interrupts */
    writel(adapter_interrupts,(void *)int_reg);

    /* flush the write */
    adapter_interrupts = readl((void *)int_reg);

    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * This routine is used to handle the "bottom half" processing for the
 * serial driver, known also the "software interrupt" processing.
 * This processing is done at the kernel interrupt level, after the
 * icom_interrupt() has returned, BUT WITH INTERRUPTS TURNED ON.  This
 * is where time-consuming activities which can not be done in the
 * interrupt driver proper are done; the interrupt driver schedules
 * them using rs_sched_event(), and they get done here.
 */
static void do_softint(void *private_)
{
    struct icom_port	*info = (struct icom_port *) private_;
    struct tty_struct	*tty;

    tty = info->tty;
    if (!tty)
        return;

    if (test_and_clear_bit(0, &info->event)) {
        tty_wakeup(tty);
        trace_nolock(info, TRACE_WAKEUP,0);
    }
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static int icom_init(void)
{
    int               index, 
    index_v2,
    index2,
    scan_index;
    struct pci_dev   *dev[MAX_ADAPTERS];
    unsigned int      irq_number[MAX_ADAPTERS];
    unsigned long     base_addr_pci[MAX_ADAPTERS];
    unsigned char     valid_indices[MAX_ADAPTERS];
#define VALID 1
#define INVALID 0
    unsigned int      command_reg;
    struct icom_port *icom_port_ptr;
    int               retval;
    int               status;
    int               port_num;
    int               adapter_count = 0;
    int               duplicate;
    unsigned int      subsystem_id;
    int               minor_number = 0;


    /*
     * Find base addresses and IRQs for any/all installed cards
     */
    for (index=0; index < MAX_ADAPTERS; index++) {
        valid_indices[index] = INVALID;
        dev[index] = NULL;
    }

    /* check for Version 1 Adapters */
    for (index = 0; index < MAX_ADAPTERS; index++){
        if (index == 0) {
            if (!(dev[index] = pci_find_device(PCI_VENDOR_ID_IBM, ICOM_DEV_ID_1, dev[index])))
                break;
        }
        else {
            if (!(dev[index] = pci_find_device(PCI_VENDOR_ID_IBM, ICOM_DEV_ID_1, dev[index-1])))
                break;
        }

        adapter_count++;

        if (pci_enable_device(dev[index])) {
            printk(KERN_ERR"icom:  Device enable FAILED\n");
            continue;
        }

        if (pci_read_config_dword(dev[index], PCI_COMMAND, &command_reg)) {
            printk(KERN_ERR"icom:  PCI Config read FAILED\n");
            continue;
        }	

        pci_write_config_dword(dev[index],PCI_COMMAND, command_reg | 0x00000146);
        pci_write_config_dword(dev[index],0x44, 0x8300830A);

        base_addr_pci[index] = pci_resource_start(dev[index],0);

        duplicate = 0;
        for (index2 = 0; index2 < index; index2++) {
            if (base_addr_pci[index] == base_addr_pci[index2])
                duplicate = 1;
        }
        if (duplicate) continue;

        irq_number[index] = dev[index]->irq;

        valid_indices[index] = ADAPTER_V1;
    }

    /* check for version 2 Adapters */
    for (index_v2=0; index < MAX_ADAPTERS; index_v2++){
        if (index_v2 == 0) {
            if (!(dev[index] = pci_find_device(PCI_VENDOR_ID_IBM, ICOM_DEV_ID_2, NULL))) {
                break;
            }
        }
        else {
            if (!(dev[index] = pci_find_device(PCI_VENDOR_ID_IBM, ICOM_DEV_ID_2, dev[index-1]))) {
                break;
            }
        }

        adapter_count++;

        if (pci_enable_device(dev[index])) {
            printk(KERN_ERR"icom:  Device enable FAILED\n");
            continue;
        }

        if (pci_read_config_dword(dev[index], PCI_COMMAND, &command_reg)) {
            printk(KERN_ERR"icom:  PCI Config read FAILED\n");
            continue;
        }	

        pci_write_config_dword(dev[index],PCI_COMMAND, command_reg | 0x00000146);
        pci_write_config_dword(dev[index],0x44, 0x42004200);
        pci_write_config_dword(dev[index],0x48, 0x42004200);

        base_addr_pci[index] = pci_resource_start(dev[index],0);	

        duplicate = 0;
        for (index2 = 0; index2 < index; index2++) {
            if (base_addr_pci[index] == base_addr_pci[index2])
                duplicate = 1;
        }
        if (duplicate) continue;

        irq_number[index] = dev[index]->irq;

        valid_indices[index++] = ADAPTER_V2;
    }

    /* allocate memory for control blocks representing each adapter */
    icom_adapter_info = (struct icom_adapter *)
        kmalloc(adapter_count*sizeof(struct icom_adapter),GFP_KERNEL);

    if (!icom_adapter_info) {
        return -ENOMEM;
    }

    memset(icom_adapter_info, 0,adapter_count*sizeof(struct icom_adapter));

    /* store information just obtained on base_addr and irq */
    for (index = scan_index = 0; (scan_index < MAX_ADAPTERS) &
        (index < adapter_count);       scan_index++) {

        if (valid_indices[scan_index]) {
            icom_adapter_info[index].base_addr_pci = base_addr_pci[scan_index];
            icom_adapter_info[index].irq_number = irq_number[scan_index];
            icom_adapter_info[index].pci_dev = dev[scan_index];
            icom_adapter_info[index].version = valid_indices[scan_index];
            pci_read_config_dword(dev[scan_index], PCI_SUBSYSTEM_VENDOR_ID, &subsystem_id);
            icom_adapter_info[index].subsystem_id = subsystem_id;

            if (icom_adapter_info[index].version == ADAPTER_V1) {
                icom_adapter_info[index].numb_ports = 2;
                icom_adapter_info[index].port_info[0].port = 0;
                icom_adapter_info[index].port_info[0].minor_number = minor_number;
                icom_adapter_info[index].port_info[0].status = ICOM_PORT_ACTIVE;
                icom_adapter_info[index].port_info[0].imbed_modem = ICOM_UNKNOWN;
                icom_adapter_info[index].port_info[1].port = 1;
                icom_adapter_info[index].port_info[1].minor_number = minor_number + 1;
                icom_adapter_info[index].port_info[1].status = ICOM_PORT_ACTIVE;
                icom_adapter_info[index].port_info[1].imbed_modem = ICOM_UNKNOWN;
            }
            else {
                if (subsystem_id == FOUR_PORT_MODEL) {
                    icom_adapter_info[index].numb_ports = 4;

                    icom_adapter_info[index].port_info[0].port = 0;
                    icom_adapter_info[index].port_info[0].minor_number = minor_number;
                    icom_adapter_info[index].port_info[0].status = ICOM_PORT_ACTIVE;
                    icom_adapter_info[index].port_info[0].imbed_modem = ICOM_IMBED_MODEM;

                    icom_adapter_info[index].port_info[1].port = 1;
                    icom_adapter_info[index].port_info[1].minor_number = minor_number + 1;
                    icom_adapter_info[index].port_info[1].status = ICOM_PORT_ACTIVE;
                    icom_adapter_info[index].port_info[1].imbed_modem = ICOM_IMBED_MODEM;

                    icom_adapter_info[index].port_info[2].port = 2;
                    icom_adapter_info[index].port_info[2].minor_number = minor_number + 2;
                    icom_adapter_info[index].port_info[2].status = ICOM_PORT_ACTIVE;
                    icom_adapter_info[index].port_info[2].imbed_modem = ICOM_IMBED_MODEM;

                    icom_adapter_info[index].port_info[3].port = 3;
                    icom_adapter_info[index].port_info[3].minor_number = minor_number + 3;
                    icom_adapter_info[index].port_info[3].status = ICOM_PORT_ACTIVE;
                    icom_adapter_info[index].port_info[3].imbed_modem = ICOM_IMBED_MODEM;
                }
                else {
                    icom_adapter_info[index].numb_ports = 4;

                    icom_adapter_info[index].port_info[0].port = 0;
                    icom_adapter_info[index].port_info[0].minor_number = minor_number;
                    icom_adapter_info[index].port_info[0].status = ICOM_PORT_ACTIVE;
                    if (subsystem_id == V2_ONE_PORT_RVX_ONE_PORT_IMBED_MDM)
                        icom_adapter_info[index].port_info[0].imbed_modem = ICOM_IMBED_MODEM;
                    else
                        icom_adapter_info[index].port_info[0].imbed_modem = ICOM_RVX;

                    icom_adapter_info[index].port_info[1].status = ICOM_PORT_OFF;

                    icom_adapter_info[index].port_info[2].port = 2;
                    icom_adapter_info[index].port_info[2].minor_number = minor_number + 1;
                    icom_adapter_info[index].port_info[2].status = ICOM_PORT_ACTIVE;
                    icom_adapter_info[index].port_info[2].imbed_modem = ICOM_RVX;

                    icom_adapter_info[index].port_info[3].status = ICOM_PORT_OFF;
                }
            }
            minor_number += 4;

            if (!request_mem_region(icom_adapter_info[index].base_addr_pci,
                                    pci_resource_len(icom_adapter_info[index].pci_dev,0),
                                    "icom")) {
                printk(KERN_ERR"icom:  request_mem_region FAILED\n");
            }

            icom_adapter_info[index].base_addr =
		      (unsigned long) ioremap(icom_adapter_info[index].base_addr_pci,
					      pci_resource_len(icom_adapter_info[index].pci_dev, 0));

            retval = diag_main(&icom_adapter_info[index]);

            if(retval)  {
                release_mem_region(icom_adapter_info[index].base_addr_pci,
                                   pci_resource_len(icom_adapter_info[index].pci_dev,0));
          	    pci_disable_device(dev[index]);
                continue;	
            }

            /* save off irq and request irq line */
            if (request_irq(irq_number[scan_index], icom_interrupt, SA_INTERRUPT |
                            SA_SHIRQ, ICOM_DRIVER_NAME, (void *)&icom_adapter_info[index])) {
                release_mem_region(icom_adapter_info[index].base_addr_pci,
                                   pci_resource_len(icom_adapter_info[index].pci_dev,0));
                pci_disable_device(dev[index]);
                printk(KERN_ERR"icom:  request_irq FAILED\n");
                continue;
            }

            for (port_num = 0; port_num < icom_adapter_info[index].numb_ports; port_num++) {
                icom_port_ptr = &icom_adapter_info[index].port_info[port_num];

                if (icom_port_ptr->status == ICOM_PORT_ACTIVE) {
                    /* initialize wait queues */
                    init_waitqueue_head(&icom_port_ptr->open_wait);
                    init_waitqueue_head(&icom_port_ptr->close_wait);
                    init_waitqueue_head(&icom_port_ptr->delta_msr_wait);

                    /* initialize port specific variables */
                    icom_port_ptr->tqueue.routine = do_softint;
                    icom_port_ptr->tqueue.data = icom_port_ptr;
                    if (icom_adapter_info[index].version == ADAPTER_V1) {
                        icom_port_ptr->global_reg = (struct icom_regs *)((char *)icom_adapter_info[index].base_addr + 0x4000);
                        icom_port_ptr->int_reg = (unsigned long)icom_adapter_info[index].base_addr + 0x4004 + 2 - 2 * port_num;
                    }
                    else {
                        icom_port_ptr->global_reg = (struct icom_regs *)((char *)icom_adapter_info[index].base_addr + 0x8000);
                        if (icom_port_ptr->port < 2)
                            icom_port_ptr->int_reg = (unsigned long)icom_adapter_info[index].base_addr + 0x8004 + 2 - 2 * icom_port_ptr->port;
                        else
                            icom_port_ptr->int_reg = (unsigned long)icom_adapter_info[index].base_addr + 0x8024 + 2 - 2 * (icom_port_ptr->port - 2);
                    }
                    icom_port_ptr->dram = (struct func_dram*)((char*)icom_adapter_info[index].base_addr + 0x2000 * icom_port_ptr->port);
                    icom_port_ptr->close_delay = 5*HZ/10;
                    icom_port_ptr->closing_wait = 30*HZ;
                    icom_port_ptr->adapter = index;

                    /*
                     * Load and start processor
                     */
                    load_code(icom_port_ptr);

                    /* get port memory */
                    if ((status = get_port_memory(icom_port_ptr)) != 0) {
                        printk(KERN_ERR"icom:  memory allocation for minor number %d port FAILED\n",icom_port_ptr->minor_number);

                        /* Fail the port, though not technically correct to call the port bad
                         due to diagnostics the end result is this port should not be accessed
                         */
                        icom_port_ptr->passed_diags = 0;
                        continue;
                    }
                }
            }
            index++;
        }
    }
    active_adapters = index;

    printk(KERN_INFO"icom:  Adapter detection complete, %d adapters found with %d valid\n",adapter_count,active_adapters);

    if (active_adapters > 0) {

        /* Initialize the tty_driver structure */
        memset(&serial_driver, 0, sizeof(struct tty_driver));
        serial_driver.magic = TTY_DRIVER_MAGIC;
        serial_driver.driver_name = ICOM_DRIVER_NAME;
#if defined(CONFIG_DEVFS_FS)
        serial_driver.name = "ttyA%d";
#else
        serial_driver.name = "ttyA";
#endif
        serial_driver.major = 243;
        serial_driver.minor_start = 0;
        serial_driver.num = NR_PORTS;
        serial_driver.type = TTY_DRIVER_TYPE_SERIAL;
        serial_driver.subtype = SERIAL_TYPE_NORMAL;
        serial_driver.init_termios = tty_std_termios;
        serial_driver.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
        serial_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
        serial_driver.refcount = &serial_refcount;
        serial_driver.table = serial_table;
        serial_driver.termios = serial_termios;
        serial_driver.termios_locked = serial_termios_locked;

        serial_driver.open = icom_open;
        serial_driver.close = icom_close;
        serial_driver.write = icom_write;
        serial_driver.put_char = icom_put_char;
        serial_driver.flush_chars = icom_flush_chars;
        serial_driver.write_room = icom_write_room;
        serial_driver.chars_in_buffer = icom_chars_in_buffer;
        serial_driver.flush_buffer = icom_flush_buffer;
        serial_driver.ioctl = icom_ioctl;
        serial_driver.throttle = icom_throttle;
        serial_driver.unthrottle = icom_unthrottle;
        serial_driver.send_xchar = icom_send_xchar;
        serial_driver.set_termios = icom_set_termios;
        serial_driver.stop = icom_stop;
        serial_driver.start = icom_start;
        serial_driver.hangup = icom_hangup;
        serial_driver.break_ctl = icom_break;
        serial_driver.wait_until_sent = icom_wait_until_sent;
        serial_driver.read_proc = icom_read_proc;


        for (index=0; index < active_adapters; index++) {
            icom_adapter_info[index].port_info[0].callout_termios = serial_driver.init_termios;
            icom_adapter_info[index].port_info[0].normal_termios = serial_driver.init_termios;
            icom_adapter_info[index].port_info[1].callout_termios = serial_driver.init_termios;
            icom_adapter_info[index].port_info[1].normal_termios = serial_driver.init_termios;
            icom_adapter_info[index].port_info[2].callout_termios = serial_driver.init_termios;
            icom_adapter_info[index].port_info[2].normal_termios = serial_driver.init_termios;
            icom_adapter_info[index].port_info[3].callout_termios = serial_driver.init_termios;
            icom_adapter_info[index].port_info[3].normal_termios = serial_driver.init_termios;
        }

        if (tty_register_driver(&serial_driver)) {
            for (index=0; index < active_adapters; index++) {
                free_irq(icom_adapter_info[index].irq_number, (void *)&icom_adapter_info[index]);
            }
            kfree(icom_adapter_info);
            panic("Couldn't register serial driver\n");
        }

        for (index = 0; index < active_adapters; index++) {
            tty_register_devfs(&serial_driver,
                               0, index*4 + serial_driver.minor_start);
            tty_register_devfs(&serial_driver,
                               0, index*4 + serial_driver.minor_start + 1);

            if ((icom_adapter_info[index].version == ADAPTER_V2) &&
                (icom_adapter_info[index].subsystem_id == FOUR_PORT_MODEL)) {
                tty_register_devfs(&serial_driver,
                                   0, index*4 + serial_driver.minor_start + 2);
                tty_register_devfs(&serial_driver,
                                   0, index*4 + serial_driver.minor_start + 3);
            }
        }

        /* lastly, register unique ioctl */
        register_ioctl32_conversion(0x4300,NULL);

#ifdef CONFIG_PPC_ISERIES
	iCom_sercons_init();
#endif

        return 0;
    }
    else {
        if (adapter_count > 0) {
            kfree(icom_adapter_info);
        }
    }

    return -ENODEV;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
int init_module(void)
{
    return icom_init();
}

/*
 * Context:  Task Level
 * Locks:    none
 */
void cleanup_module(void) 
{
    int                 e1;
    int                 index;
    int                 port_num;
    struct icom_port   *icom_port_ptr;

    /* remove registered ioctl */
    unregister_ioctl32_conversion(0x4300);

    if ((e1 = tty_unregister_driver(&serial_driver)))
        printk(KERN_ERR"icom:  failed to unregister serial driver (%d)\n",e1);

    for (index = 0; index < active_adapters; index++) {
        tty_unregister_devfs(&serial_driver,
                             index*4 + serial_driver.minor_start);
        tty_unregister_devfs(&serial_driver,
                             index*4 + serial_driver.minor_start + 1);

        if ((icom_adapter_info[index].version == ADAPTER_V2) &&
            (icom_adapter_info[index].subsystem_id == FOUR_PORT_MODEL)) {
            tty_unregister_devfs(&serial_driver,
                                 index*4 + serial_driver.minor_start + 2);
            tty_unregister_devfs(&serial_driver,
                                 index*4 + serial_driver.minor_start + 3);
        }
    }

    for (index=0; index < active_adapters; index++) {

        for (port_num = 0; port_num < icom_adapter_info[index].numb_ports; port_num++) {
            icom_port_ptr = &icom_adapter_info[index].port_info[port_num];

            if (icom_port_ptr->status == ICOM_PORT_ACTIVE) {

                /* be sure that DTR and RTS are dropped */
                writeb(0x00,&icom_port_ptr->dram->osr);

                /* Wait 0.1 Sec for simple Init to complete */
                current->state = TASK_UNINTERRUPTIBLE;
                schedule_timeout(HZ/10);

                /* Stop proccessor */
                stop_processor(icom_port_ptr);

                return_port_memory(icom_port_ptr);
            }
        }

        free_irq(icom_adapter_info[index].irq_number, (void *)&icom_adapter_info[index]);
        release_mem_region(icom_adapter_info[index].base_addr_pci,
                           pci_resource_len(icom_adapter_info[index].pci_dev,0));
        pci_disable_device(icom_adapter_info[index].pci_dev);
    }

    kfree(icom_adapter_info);
    printk(KERN_INFO"icom:  Driver removed\n");
}

#ifdef ICOM_TRACE
/*
 * Context:  any
 * Locks:    none
 */
static void trace_nolock(struct icom_port *icom_port_ptr,
                  u32 trace_pt, u32 trace_data)
{
    unsigned long  flags;

    spin_lock_irqsave(&icom_lock,flags);
    trace_lock(icom_port_ptr, trace_pt, trace_data);
    spin_unlock_irqrestore(&icom_lock,flags);
}

/*
 * Context:  any
 * Locks:    icom_lock
 */
static void trace_lock(struct icom_port *icom_port_ptr,
                  u32 trace_pt, u32 trace_data)
{
    u32 *tp_start, *tp_end, **tp_next;

    if (icom_port_ptr->trace_blk == 0) return;

    tp_start  = (u32 *)icom_port_ptr->trace_blk[0];
    tp_end    = (u32 *)icom_port_ptr->trace_blk[1];
    tp_next   = (u32 **)&icom_port_ptr->trace_blk[2];

    if (trace_data != 0) {
        **tp_next = trace_data;
        *tp_next = *tp_next + 1;
        if (*tp_next == tp_end) *tp_next = tp_start;
        **tp_next = TRACE_WITH_DATA | trace_pt;
    }
    else
        **tp_next = trace_pt;

    *tp_next = *tp_next + 1;
    if (*tp_next == tp_end) *tp_next = tp_start;
}

/*
 * Context:  Task Level
 * Locks:    none
 */
static void trace_mem_mgr(struct icom_port *icom_port_ptr, u32 trace_pt,
           u32 trace_data) {

    if (trace_pt == TRACE_GET_MEM) {
        if (icom_port_ptr->trace_blk != 0) return;
        icom_port_ptr->trace_blk = kmalloc(TRACE_BLK_SZ,GFP_KERNEL);

        if (!icom_port_ptr->trace_blk) {
            /* unable to get memory, this is only for internal
             tracing/debug, no need to post any messages */
            return;
        }

        memset(icom_port_ptr->trace_blk, 0,TRACE_BLK_SZ);
        icom_port_ptr->trace_blk[0] = (unsigned long)icom_port_ptr->trace_blk + 3*sizeof(unsigned long);
        icom_port_ptr->trace_blk[1] = (unsigned long)icom_port_ptr->trace_blk + TRACE_BLK_SZ;
        icom_port_ptr->trace_blk[2] = icom_port_ptr->trace_blk[0];
    }
    if (icom_port_ptr->trace_blk == 0) return;

    if (trace_pt == TRACE_RET_MEM) {
        kfree(icom_port_ptr->trace_blk);
        icom_port_ptr->trace_blk = 0;
        return;
    }
}

#endif

    /****************************************************/
    /* Diagnostic and insmod/icom_init support routines */
    /****************************************************/

/*******************************************/
/* Diagnostic Check for previous Error Log */
/*******************************************/
static u8 __init NoErrorYet(struct icom_adapter *icom_adapter_ptr,
                            u8	port)  /* IOA_FAILURE indicates an Adapter failure */
{
    if(port == IOA_FAILURE) {
        if(icom_adapter_ptr->error_data[0] == 0)
            return -1; /* return non zero (TRUE) */
    }
    else {
        if(icom_adapter_ptr->port_info[port].tpr == 0)
            return -1; /* return non zero (TRUE) */
    }

    /* must be an error so return 0 (FALSE) */
    return 0;
}

/************************************/
/* Diagnostic Error Logging Routine */
/************************************/
static void __init diag_error(struct icom_adapter *icom_adapter_ptr,
                       u8	port,  /* IOA_FAILURE indicates an Adapter failure */
                       char*	text, /* error message */
                       u32	tpr,
                       u32*	error_data_ptr,
                       u32	num_error_entries)

{
    u32	i,j;
    u32 error_data[NUM_ERROR_ENTRIES];
    u32 *temp32ptr;
    int version;
    char port_letter = '0';
    struct LocationDataStruct *p_location_data = NULL;


    if((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2)
        version = 1;
    else
        version = 2;

    if(port == 0)
        port_letter = 'A';
    else if(port == 1)
        port_letter = 'B';
    else if(port == 2)
        port_letter = 'C';
    else if(port == 3)
        port_letter = 'D';

    if(NoErrorYet(icom_adapter_ptr, port)) {
        if(port == IOA_FAILURE) {
            icom_adapter_ptr->tpr = tpr;
            temp32ptr = &icom_adapter_ptr->error_data[0];

            /* set all ports to failed */
            for(j=0;j<NUM_ERROR_ENTRIES;j++) {
                icom_adapter_ptr->port_info[j].passed_diags = 0; /* failed */
            }

            /* find the MIN of num_error_entries and NUM_ERROR_ENTRIES */
            if(num_error_entries < NUM_ERROR_ENTRIES)
                j=num_error_entries;
            else
                j=NUM_ERROR_ENTRIES;
            for(i=0;i<j;i++) {
                temp32ptr[i] = error_data_ptr[i];
            }
        }
        else {
            icom_adapter_ptr->port_info[port].tpr = tpr;
            temp32ptr = &icom_adapter_ptr->port_info[port].error_data[0];
            icom_adapter_ptr->port_info[port].passed_diags = 0;/* failed */

            /* find the MIN of num_error_entries and NUM_ERROR_ENTRIES */
            if(num_error_entries < NUM_ERROR_ENTRIES)
                j=num_error_entries;
            else
                j=NUM_ERROR_ENTRIES;
            for(i=0;i<j;i++) {
                temp32ptr[i] = error_data_ptr[i];
            }
        }

        /* copy error data into local array */
        for(i=0;i<j;i++)
            error_data[i] = error_data_ptr[i];

        /* set the rest of the error data array to zero */
        for(;i<NUM_ERROR_ENTRIES;i++)
            error_data[i] = 0;

        printk(KERN_ERR"icom: ERROR #################################################\n");
#ifdef CONFIG_PPC_ISERIES
        p_location_data = iSeries_GetLocationData(icom_adapter_ptr->pci_dev);

        if (p_location_data != NULL) {
            printk(KERN_ERR"icom: Frame ID: %d Card Position: %s.\n",p_location_data->FrameId, p_location_data->CardLocation);
            kfree(p_location_data);
        }
#endif
        if(port == IOA_FAILURE)
            printk(KERN_ERR"icom: Version %d adapter failed.\n",version);
        else
            printk(KERN_ERR"icom: Version %d adapter port %c failed.\n",version, port_letter);
        printk(KERN_ERR"icom: ");
        printk(text); /* print text */
        /* print sixteen words of error data */
        printk(KERN_ERR"icom: %.8x %.8x %.8x %.8x\n", tpr, error_data[0], error_data[1], error_data[2]);
        printk(KERN_ERR"icom: %.8x %.8x %.8x %.8x\n", error_data[3], error_data[4], error_data[5], error_data[6]);
        printk(KERN_ERR"icom: %.8x %.8x %.8x %.8x\n", error_data[7], error_data[8], error_data[9], error_data[10]);
        printk(KERN_ERR"icom: %.8x %.8x %.8x %.8x\n", error_data[11], error_data[12], error_data[13], error_data[14]);

        printk(KERN_ERR"icom: ERROR #################################################\n");
    }
    return;
}

/**************************************************/
/* Diagnostic Interrupt register checking routine */
/**************************************************/
static u32 __init diag_check_ints(struct icom_adapter *icom_adapter_ptr,
                                  u32 exp_int1,		/* expected int one value */
                                  u32 exp_int2,		/* expected int two value */
                                  char *msg,			/* message to put in error log */
                                  u32 tpr)			/* TPR to put in error log */
{
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];

#define PORT_A_INT_MASK 0x0000FFFF
#define PORT_B_INT_MASK 0xFFFF0000


    u32	status = 0;	/* pass/fail */

    if ((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2){
        /* check port A */
        if((icom_adapter_ptr->diag_int_reset1 & PORT_A_INT_MASK )
           != (exp_int1 & PORT_A_INT_MASK)) {
            error_data[0] = exp_int1;
            error_data[1] = icom_adapter_ptr->diag_int_reset1;

            diag_error(icom_adapter_ptr,
                       0,  /* IOA_FAILURE indicates an Adapter failure */
                       msg, /* error message */
                       tpr,		/* tpr */
                       error_ptr,		/* error data pointer */
                       2);	/* number of error words. */
            status = -1;
        }

        /* check port B */
        if((icom_adapter_ptr->diag_int_reset1 & PORT_B_INT_MASK )
           != (exp_int1 & PORT_B_INT_MASK)) {
            error_data[0] = exp_int1;
            error_data[1] = icom_adapter_ptr->diag_int_reset1;

            diag_error(icom_adapter_ptr,
                       1,  /* IOA_FAILURE indicates an Adapter failure */
                       msg, /* error message */
                       tpr,		/* tpr */
                       error_ptr,		/* error data pointer */
                       2);	/* number of error words. */
            status = -1;
        }
    }
    else { /* this is a four port adapter */
        if((icom_adapter_ptr->diag_int_reset1 & PORT_A_INT_MASK )
           != (exp_int1 & PORT_A_INT_MASK)) {
            error_data[0] = exp_int1;
            error_data[1] = icom_adapter_ptr->diag_int_reset1;

            diag_error(icom_adapter_ptr,
                       0,  /* IOA_FAILURE indicates an Adapter failure */
                       msg, /* error message */
                       tpr,		/* tpr */
                       error_ptr,		/* error data pointer */
                       2);	/* number of error words. */
            status = -1;
        }
        /* check port B */
        if((icom_adapter_ptr->diag_int_reset1 & PORT_B_INT_MASK )
           != (exp_int1 & PORT_B_INT_MASK))  {
            error_data[0] = exp_int1;
            error_data[1] = icom_adapter_ptr->diag_int_reset1;

            diag_error(icom_adapter_ptr,
                       1,  /* IOA_FAILURE indicates an Adapter failure */
                       msg, /* error message */
                       tpr,		/* tpr */
                       error_ptr,		/* error data pointer */
                       2);	/* number of error words. */
            status = -1;
        }
        /* check port C */
        if((icom_adapter_ptr->diag_int_reset2 & PORT_A_INT_MASK )
           != (exp_int2 & PORT_A_INT_MASK)) {
            error_data[0] = exp_int2;
            error_data[1] = icom_adapter_ptr->diag_int_reset2;

            diag_error(icom_adapter_ptr,
                       2,  /* IOA_FAILURE indicates an Adapter failure */
                       msg, /* error message */
                       tpr,		/* tpr */
                       error_ptr,		/* error data pointer */
                       2);	/* number of error words. */
            status = -1;
        }
        /* check port D */
        if((icom_adapter_ptr->diag_int_reset2 & PORT_B_INT_MASK )
           != (exp_int2 & PORT_B_INT_MASK)) {
            error_data[0] = exp_int2;
            error_data[1] = icom_adapter_ptr->diag_int_reset2;

            diag_error(icom_adapter_ptr,
                       3,  /* IOA_FAILURE indicates an Adapter failure */
                       msg, /* error message */
                       tpr,		/* tpr */
                       error_ptr,		/* error data pointer */
                       2);	/* number of error words. */
            status = -1;
        }
    }
    return status;
}

/*************************************/
/* Diagnostic Initialization Routine */
/*************************************/
static u32 __init diag_init(struct icom_adapter *icom_adapter_ptr)
{
    u8	i,j;
    u32	rc;

    /* init rc to zero */
    rc = 0;

    icom_adapter_ptr->tpr = DIAG_INIT_ERROR_DATA;
    /* set the TPR and error data to zero for the adapter */
    for(j=0;j<NUM_ERROR_ENTRIES;j++)
        icom_adapter_ptr->error_data[j] = 0;

    /* set the TPR and error data to zero for all ports */
    for(i=0;i<icom_adapter_ptr->numb_ports;i++) {
        icom_adapter_ptr->port_info[i].tpr = 0;
        icom_adapter_ptr->port_info[i].passed_diags = 0; /* false */
        for(j=0;j<NUM_ERROR_ENTRIES;j++) {
            icom_adapter_ptr->port_info[i].error_data[j] = 0;
        }
    }

    /* Set the global register value */
    icom_adapter_ptr->tpr = DIAG_INIT_SET_GLOBAL_REGS;
    if (icom_adapter_ptr->version == ADAPTER_V1) {
        icom_adapter_ptr->port_info[0].global_reg = (struct icom_regs *)((char *)icom_adapter_ptr->base_addr + 0x4000);
        icom_adapter_ptr->port_info[1].global_reg = (struct icom_regs *)((char *)icom_adapter_ptr->base_addr + 0x4000);
    }
    else {
        icom_adapter_ptr->port_info[0].global_reg = (struct icom_regs *)((char *)icom_adapter_ptr->base_addr + 0x8000);
        icom_adapter_ptr->port_info[1].global_reg = (struct icom_regs *)((char *)icom_adapter_ptr->base_addr + 0x8000);
        icom_adapter_ptr->port_info[2].global_reg = (struct icom_regs *)((char *)icom_adapter_ptr->base_addr + 0x8000);
        icom_adapter_ptr->port_info[3].global_reg = (struct icom_regs *)((char *)icom_adapter_ptr->base_addr + 0x8000);
    }

    icom_adapter_ptr->tpr = DIAG_INIT_END;
    return rc;
}


/********************************/
/* Diagnostic Interrupt handler */
/********************************/
static void __init diag_int_handler(int irq, void * dev_id, struct pt_regs * regs)
{
    u8                  temp_8;
    u32                 temp_32;
    struct icom_adapter *icom_adapter_ptr;
    /* find icom_port_info for this interrupt */
    icom_adapter_ptr = (struct icom_adapter *)dev_id;


    /* if BAR0 is 0, then this must be a BIST */
    /* int so reset the config regs           */
    pci_read_config_dword(icom_adapter_ptr->pci_dev, PCI_COMMAND, &temp_32);

    /* if memory space is not enabled, this must be the BIST interrupt */
    /* note that version 2 adapters will already be set up so it will  */
    /* skip this piece of code.                                        */
    if(!(temp_32 & 0x00000002)) {
        /* restore the config registers */
        pci_write_config_dword(icom_adapter_ptr->pci_dev,PCI_COMMAND, icom_adapter_ptr->saved_command_reg);
        pci_write_config_dword(icom_adapter_ptr->pci_dev,0x44, 0x8300830A);
        pci_write_config_dword(icom_adapter_ptr->pci_dev, PCI_BASE_ADDRESS_0, icom_adapter_ptr->saved_bar);
    }

    icom_adapter_ptr->tpr = DIAG_INTHANDLE_START;
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        icom_adapter_ptr->diag_int1 = readl((void*)(icom_adapter_ptr->base_addr+0x8004));
        icom_adapter_ptr->diag_int2 = readl((void*)(icom_adapter_ptr->base_addr+0x8024));
        icom_adapter_ptr->diag_int_pri1 = readl((void*)(icom_adapter_ptr->base_addr+0x800C));
        icom_adapter_ptr->diag_int_pri2 = readl((void*)(icom_adapter_ptr->base_addr+0x802C));
    }
    else{
        icom_adapter_ptr->diag_int1 = readl((void*)(icom_adapter_ptr->base_addr+0x4004));
        icom_adapter_ptr->diag_int2 = 0; /* does not exist */
        icom_adapter_ptr->diag_int_pri1 = readl((void*)(icom_adapter_ptr->base_addr+0x400C));
        icom_adapter_ptr->diag_int_pri2 = 0x80; /* no int */
    }

    /* if pri1 has an int set, clear that int */
    icom_adapter_ptr->tpr = DIAG_INTHANDLE_CHECK_FOR_INTS;
    if(icom_adapter_ptr->diag_int_pri1 != 0x80) { /* 0x80 means "no int" */
        temp_8 = icom_adapter_ptr->diag_int_pri1 >> 2; /* determine how many bits to shift left */
        temp_32 = 1;
        temp_32 <<= temp_8; /* shift a 1 that many bits */
        icom_adapter_ptr->diag_int_reset1 |= temp_32; /* record the int being reset */
        if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
            writel(temp_32,(void*)icom_adapter_ptr->base_addr+0x8004); /* reset int */
        }
        else
            writel(temp_32,(void*)icom_adapter_ptr->base_addr+0x4004); /* reset int */
    }
    else /* clear the int from pri2 */{
        temp_8 = icom_adapter_ptr->diag_int_pri2 >> 2; /* determine how many bits to shift left */
        temp_32 = 1;
        temp_32 <<= temp_8; /* shift a 1 that many bits */
        icom_adapter_ptr->diag_int_reset2 |= temp_32; /* record the int being reset */
        writel(temp_32,(void*)icom_adapter_ptr->base_addr+0x8024); /* reset int */
    }
    icom_adapter_ptr->tpr = DIAG_INTHANDLE_END;
    return;

}


/************************/
/* Diagnostic BIST Test */
/************************/
static u32 __init diag_bist(struct icom_adapter *icom_adapter_ptr)
{

    u32           rc;
    u32           temp_32;
    u8            i;
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];
    int		done;

    icom_adapter_ptr->tpr = DIAG_BIST_CLEAR_INTS;
    /* make sure the all ints are cleared before we start */
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        writel(0xFFFFFFFF,(void*)(icom_adapter_ptr->base_addr+0x8004)); 
        writel(0xFFFFFFFF,(void*)(icom_adapter_ptr->base_addr+0x8024));
    }
    else{
        writel(0xFFFFFFFF,(void*)(icom_adapter_ptr->base_addr+0x4004)); 
    }

    /* version 2 adapters must not cause a real int because */
    /* accesses to any register immediately after BIST will */
    /* fail. We must first mask the ints, cause BIST to     */
    /* happen, then unmask the int to get to the interrupt  */
    /* handler. Note that version 1 adapters can not mask   */
    /* off the BIST int.                                    */
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8008));
        temp_32 |= 0x80000000;
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8008));
    }

    /**** save registers that will have to be restored ****/

    /* get the control reg */
    icom_adapter_ptr->tpr = DIAG_BIST_SAVE_CMD_REG;
    if((rc = pci_read_config_dword(icom_adapter_ptr->pci_dev, PCI_COMMAND,
                                   &icom_adapter_ptr->saved_command_reg))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Bist Test. Failed to read command reg.\n", /* error message */
                   DIAG_BIST_READ_CMD_REG_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f; /* fail the whole adapter */
    }

    /* get the memory bar reg */
    icom_adapter_ptr->tpr = DIAG_BIST_SAVE_BAR0_REG;
    if((rc = pci_read_config_dword(icom_adapter_ptr->pci_dev,
                                   PCI_BASE_ADDRESS_0,
                                   &icom_adapter_ptr->saved_bar))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Bist Test. Failed to read BAR 0.\n", /* error message */
                   DIAG_BIST_READ_BAR0_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f; /* fail the whole adapter */
    }

    /**** hook in the interrupt handler ****/

    icom_adapter_ptr->diag_int1 = 0; /* init */

    /* save off irq and request irq line */
    icom_adapter_ptr->tpr = DIAG_BIST_HOOK_INT_LINE;
    if ((rc = request_irq(icom_adapter_ptr->pci_dev->irq, diag_int_handler, SA_INTERRUPT |
                          SA_SHIRQ, ICOM_DRIVER_NAME, (void *)icom_adapter_ptr))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Bist Test. Failed to hook int handler.\n", /* error message */
                   DIAG_BIST_HOOK_INT_HANDLER_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f; /* fail the whole adapter */
    }
    else
        icom_adapter_ptr->resources |= HAVE_INT_HANDLE;

    /**** start bist ****/
    icom_adapter_ptr->tpr = DIAG_BIST_START_BIST;
    if((rc = pci_write_config_dword(icom_adapter_ptr->pci_dev, 0x0C, 0x40000000))) {
        printk(KERN_ERR"icom: Diagnostic failed. CD700230, %8x.\n", rc);
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Bist Test. Failed to write BIST reg.\n", /* error message */
                   DIAG_BIST_START_BIST_FAIL, /* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f;
    }

    /* set done to zero before the check for version 2 below */
    done = 0;

    /* restore the config registers on version 2 adapters */
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {

        /**** wait 2 seconds ****/
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ*2); /* HZ = 1 second */

        /* restore the config registers */
        pci_write_config_dword(icom_adapter_ptr->pci_dev,PCI_COMMAND, icom_adapter_ptr->saved_command_reg);
        pci_write_config_dword(icom_adapter_ptr->pci_dev,0x44, 0x42004200);
        pci_write_config_dword(icom_adapter_ptr->pci_dev,0x48, 0x42004200);
        pci_write_config_dword(icom_adapter_ptr->pci_dev, PCI_BASE_ADDRESS_0, icom_adapter_ptr->saved_bar);

        /* now clear the mask bit to force the interrupt to happen */
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8008));
        temp_32 = temp_32 & 0x7FFFFFFF;
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8008));
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/10); /* HZ = 1 second */
        done = 1;
    }

    /**** wait for 6 seconds or int ****/
    icom_adapter_ptr->tpr = DIAG_BIST_WAIT_FOR_INT;
    for(i=0;((i<60) && (done==0));i++)  {
        if(icom_adapter_ptr->diag_int1 == 0) {
            /* wait 100ms */
            current->state = TASK_UNINTERRUPTIBLE;
            schedule_timeout(HZ/10); /* HZ = 1 second */
        }
        else  {
            done = 1;
        }
    }

    /* if not passed */
    icom_adapter_ptr->tpr = DIAG_BIST_CHECK_INT_REGS;
    if(icom_adapter_ptr->diag_int1 != 0x80000000) {
        error_data[0] = icom_adapter_ptr->diag_int1;
        error_data[1] = i;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Bist Test. Bist int did not occur.\n", /* error message */
                   DIAG_BIST_NO_BIST_INT_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   2);	/* number of error words. */
        return 0x0000000f; /* fail the whole adapter */
    }

    /**** see if bist completed successfully ****/
    icom_adapter_ptr->tpr = DIAG_CHECK_READ_BIST_REG;
    if((rc = pci_read_config_dword(icom_adapter_ptr->pci_dev,
                                   0x0C, &temp_32))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Bist Test. Unable to read config offset 0xC.\n", /* error message */
                   DIAG_BIST_READ_CONFIG_OFFSET_0C_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f; /* fail the whole adapter */
    }

    /**** check tha the MISR and PRPG are not set ****/
    icom_adapter_ptr->tpr = DIAG_BIST_CHECK_MISR_AND_PRPG;
    if(temp_32 & 0x03000000) {
        error_data[0] = temp_32;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Bist Test. Incorrect MISR or PRPG.\n", /* error message */
                   DIAG_BIST_INCORRECT_MISR_OR_PRPG,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f; /* fail the whole adapter */
    }

    /**** return int handler ****/
    icom_adapter_ptr->tpr = DIAG_BIST_RETURN_INT_HANDLE;
    free_irq(icom_adapter_ptr->pci_dev->irq, (void *)icom_adapter_ptr);
    icom_adapter_ptr->resources &= ~HAVE_INT_HANDLE;

    return 0; /* pass */
}

/***************/
/* memory test */
/***************/
static u32 __init mem_test(struct icom_adapter *icom_adapter_ptr,
                           u32 port,
                           unsigned long addr,
                           u32 len)
{
    u8            data;
    u8		actual_data;
    u32           i;
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];

    /**** addr tag test ****/

    /* set the memory */
    icom_adapter_ptr->tpr = DIAG_MEM_WRITE_ADDR_TAG;
    data = 0;
    for(i=0;i<len;i++) {
        writeb(data, (void*)(addr + i));
        ++data;
        if(data == 254)
            data = 0;
    }

    /* check the memory */
    icom_adapter_ptr->tpr = DIAG_MEM_CHECK_ADDR_TAG;
    data = 0;
    for(i=0;i<len;i++) {
        actual_data = readb((void*)(addr + i));

        if(actual_data != data) {
            error_data[0] = (u32)data;
            error_data[1] = (u32)actual_data;
            error_data[2] = (u32)addr;
            error_data[3] = (u32)addr+i;

            diag_error(icom_adapter_ptr,
                       port,  /* IOA_FAILURE indicates an Adapter failure */
                       "icom: Memory Test. Address Tag Test Failed.\n", /* error message */
                       DIAG_MEM_ADDR_TAG_TEST_FAILED,		/* tpr */
                       error_ptr,		/* error data pointer */
                       4);	/* number of error words. */
            return -1; /* fail the whole adapter */
        }
        ++data;
        if(data == 254)
            data = 0;
    }

    /**** write A's ****/

    icom_adapter_ptr->tpr = DIAG_MEM_WRITE_AA;
    for(i=0;i<len;i++) {
        writeb(0xAA, (void*)(addr + i));
    }

    icom_adapter_ptr->tpr = DIAG_MEM_CHECK_AA;
    for(i=0;i<len;i++)  {
        actual_data = readb((void*)(addr + i));
        if(actual_data != 0xAA) {
            error_data[0] = 0x000000AA;
            error_data[1] = (u32)actual_data;
            error_data[2] = (u32)addr;
            error_data[3] = (u32)addr+i;

            diag_error(icom_adapter_ptr,
                       port,  /* IOA_FAILURE indicates an Adapter failure */
                       "icom: Memory Test. 0xAA Test Failed.\n", /* error message */
                       DIAG_MEM_AA_TEST_FAIL,		/* tpr */
                       error_ptr,		/* error data pointer */
                       4);	/* number of error words. */
            return -1;
        }
        writeb(0x55, (void*)(addr + i));
    }

    /**** check for 55's ****/

    icom_adapter_ptr->tpr = DIAG_MEM_CHECK_55;
    for(i=0;i<len;i++) {
        actual_data = readb((void*)(addr + i));
        if(actual_data != 0x55) {
            error_data[0] = 0x00000055;
            error_data[1] = (u32)actual_data;
            error_data[2] = (u32)addr;
            error_data[3] = (u32)addr+i;

            diag_error(icom_adapter_ptr,
                       port,  /* IOA_FAILURE indicates an Adapter failure */
                       "icom: Memory Test. 0x55 Test Failed.\n", /* error message */
                       DIAG_MEM_55_TEST_FAIL,		/* tpr */
                       error_ptr,		/* error data pointer */
                       4);	/* number of error words. */
            return -1;
        }
        writeb(0, (void*)(addr + i));
    }

    /**** check for 00's ****/

    icom_adapter_ptr->tpr = DIAG_MEM_CHECK_00;
    for(i=0;i<len;i++) {
        actual_data = readb((void*)(addr + i));
        if(actual_data != 0) {
            error_data[0] = 0x00000000;
            error_data[1] = (u32)actual_data;
            error_data[2] = (u32)addr;
            error_data[3] = (u32)addr+i;

            diag_error(icom_adapter_ptr,
                       port,  /* IOA_FAILURE indicates an Adapter failure */
                       "icom: Memory Test. 0x00 Test Failed.\n", /* error message */
                       DIAG_MEM_00_TEST_FAIL,		/* tpr */
                       error_ptr,		/* error data pointer */
                       4);	/* number of error words. */
            return -1;
        }
    }
    return 0;
}


/**************************/
/* Diagnostic Memory test */
/**************************/
static u32 __init diag_memory(struct icom_adapter *icom_adapter_ptr)
{
    u8            i;
    u32           port;
    u32           rc;
    unsigned long mem1_addr, mem2_addr;
    u32           mem1_len, mem2_len;
    struct icom_port    *icom_port_info;
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];


    /* for the number of ports on adapter */
    icom_adapter_ptr->tpr = DIAG_MEM_FOR_EVERY_PORT;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++) {

        /* Stop processor to make sure the instruction ram can be tested */
        icom_adapter_ptr->tpr = DIAG_MEM_STOP_PROC;
        icom_port_info = &icom_adapter_ptr->port_info[i];
        stop_processor(icom_port_info);

        switch (i) {
            case 0: /* this port exists on all adpaters */
                port = 1; /* bit for port 0 */
                mem1_addr = icom_adapter_ptr->base_addr;
                mem1_len = 0x200;
                mem2_addr = icom_adapter_ptr->base_addr + 0x1000;
                if(icom_adapter_ptr->version == ADAPTER_V1)
                    mem2_len = 0xc00;
                else
                    mem2_len = 0x1000;
                break;

            case 1:
                if(icom_adapter_ptr->port_info[1].status == ICOM_PORT_ACTIVE) {
                    port = 2; /* bit for port 1 */
                    mem1_addr = icom_adapter_ptr->base_addr + 0x2000;
                    mem1_len = 0x200;
                    mem2_addr = icom_adapter_ptr->base_addr + 0x3000;
                    if(icom_adapter_ptr->version == ADAPTER_V1)
                        mem2_len = 0xc00;
                    else
                        mem2_len = 0x1000;
                }
                else
                    continue;
                break;

            case 2: /* if four ports exist, this one will be valid */
                port = 4; /* bit for port 2 */
                mem1_addr = icom_adapter_ptr->base_addr + 0x4000;
                mem1_len = 0x200;
                mem2_addr = icom_adapter_ptr->base_addr + 0x5000;
                mem2_len = 0x1000;
                break;

            case 3:
                if(icom_adapter_ptr->port_info[3].status == ICOM_PORT_ACTIVE) {
                    port = 8; /* bit for port 3 */
                    mem1_addr = icom_adapter_ptr->base_addr + 0x6000;
                    mem1_len = 0x200;
                    mem2_addr = icom_adapter_ptr->base_addr + 0x7000;
                    mem2_len = 0x1000;
                }
                else
                    continue;
                break;
            default:
                error_data[0] = i;

                diag_error(icom_adapter_ptr,
                           IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                           "icom: Memory Test. Invalid Port Select.\n", /* error message */
                           DIAG_MEM_INVALID_PORT_SELECT,		/* tpr */
                           error_ptr,		/* error data pointer */
                           1);	/* number of error words. */
                return -1;

        } /* end of switch */

        /* test the memory */
        icom_adapter_ptr->tpr = DIAG_MEM_CALL_MEM_TEST;
        if((rc = mem_test(icom_adapter_ptr, port, mem1_addr, mem1_len)))
            return port;
        if((rc = mem_test(icom_adapter_ptr, port, mem2_addr, mem2_len)))
            return port;

    } /* end of for loop */

    /* pass */
    return 0;

} /* end of function */

/*****************************/
/* Diagnostic Port Load Code */
/*****************************/
static void __init diag_port_load(int port,
                           struct icom_port *icom_port_info,
                           long int base_addr,
                           unsigned char *code,
                           int size)
{
    char               *iram_ptr;
    int                index;
    char              *dram_ptr;


    /* point to the DRAM */
    dram_ptr = (char*)(base_addr + 0x2000*port);

    /* Stop processor */
    icom_port_info->tpr = DIAG_LOAD_STOP_PROC;
    stop_processor(icom_port_info);

    /* Zero out DRAM */
    icom_port_info->tpr = DIAG_LOAD_ZERO_DRAM;
    for (index = 0; index < 512; index++)  {
        writeb(0x00,&dram_ptr[index]);
    }

    /* Load Code into Adapter */
    icom_port_info->tpr = DIAG_LOAD_LOAD_CODE;
    iram_ptr = (char *)dram_ptr + ICOM_IRAM_OFFSET;
    for (index = 0; index < size; index++) {
        writeb(code[index],&iram_ptr[index]);
    }

    icom_port_info->tpr = DIAG_LOAD_DONE;
    icom_port_info->tpr = 0; /* must set this to zero when done */
    /* for IfNoError to work.          */
    return ;
} 

/*****************************/
/* Diagnostic Interrupt Test */
/*****************************/
static u32 __init diag_int(struct icom_adapter *icom_adapter_ptr)
{

    u32           temp_32;
    u32           rc;
    u8            i;
    u32           exp_int1, exp_int2;
    u8            done;
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];

    /* make sure the all ints are cleared before we start */
    icom_adapter_ptr->tpr = DIAG_INT_CLEAR_INTS;
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8004)); 
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8024));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8024)); 
    }
    else {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x4004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x4004)); 
    }

    /* save off irq and request irq line */
    icom_adapter_ptr->tpr = DIAG_INT_GET_INT_HANDLE;
    if ((rc = request_irq(icom_adapter_ptr->pci_dev->irq, diag_int_handler, SA_INTERRUPT |
                          SA_SHIRQ, ICOM_DRIVER_NAME, (void *)icom_adapter_ptr))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Interrupt Test. request_irq call failed.\n", /* error message */
                   DIAG_INT_GET_IRQ_LINE_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f;
    }
    else
        icom_adapter_ptr->resources |= HAVE_INT_HANDLE;

    /* load all active procs */
    icom_adapter_ptr->tpr = DIAG_INT_LOAD_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            diag_port_load(i,
                           &icom_adapter_ptr->port_info[i],
                           icom_adapter_ptr->base_addr,
                           topcidiag, 
                           sizeof(topcidiag));

    /* clear int regs */
    icom_adapter_ptr->tpr = DIAG_INT_CLEAR_INT_VARS;
    icom_adapter_ptr->diag_int_reset1 = 0;
    icom_adapter_ptr->diag_int_reset2 = 0;

    /* unmask all interrupts */
    if ((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2)
        writel(0,(void*)(icom_adapter_ptr->base_addr+0x4008));
    else  {
        writel(0,(void*)(icom_adapter_ptr->base_addr+0x8008));
        writel(0,(void*)(icom_adapter_ptr->base_addr+0x8028));
    }

    /* Start all procs */
    icom_adapter_ptr->tpr = DIAG_INT_START_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)  {
            start_processor(&icom_adapter_ptr->port_info[i]);
        }

    /* Start all tests */
    icom_adapter_ptr->tpr = DIAG_INT_START_TESTS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE) {
            /* 0x06 is the interrupt test */
            writeb((0x06),(void*)(icom_adapter_ptr->base_addr+i*0x2000)); 
        }

    /* determine what ints to look for */
    icom_adapter_ptr->tpr = DIAG_INT_DETERMINE_INTS_TO_CHECK_FOR;
    if ((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2) {
        exp_int1 = (u32)0x3FFF3FFF;
        exp_int2 = 0;
    }
    else {
        exp_int1 = 0x00003FFF;
        exp_int2 = 0x00003FFF;
        if(icom_adapter_ptr->port_info[1].status == ICOM_PORT_ACTIVE)
            exp_int1 |= 0x3FFF0000;
        if(icom_adapter_ptr->port_info[3].status == ICOM_PORT_ACTIVE)
            exp_int2 |= 0x3FFF0000;
    }

    /* wait for all ints to set or for a second to pass */
    done = 0;
    temp_32 = 0;
    icom_adapter_ptr->tpr = DIAG_INT_WAIT_FOR_INTS;
    while((!done) && (temp_32 < 100))  {
        if(((icom_adapter_ptr->diag_int_reset1 & 0x3fff3fff) == exp_int1) &&
           ((icom_adapter_ptr->diag_int_reset2 & 0x3fff3fff) == exp_int2))
            done = 1;

        /**** wait 1 millisecond ****/
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/100); /* HZ = 1 second */
        temp_32++;
    }

    temp_32 = 0;

    icom_adapter_ptr->tpr = DIAG_INT_CHECK_FOR_INTS;
    if (!done) { /* failed */ 
        /* fail the appropriate port/s */
        rc = diag_check_ints(icom_adapter_ptr,
                             exp_int1,
                             exp_int2,
                             "Interrupt Test. Int did not occur.\n",
                             DIAG_INT_WRONG_INT_FAIL);
        return -1;
    }


    /* stop all procs */
    icom_adapter_ptr->tpr = DIAG_INT_STOP_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            stop_processor(&icom_adapter_ptr->port_info[i]);

    /**** return int handler ****/
    icom_adapter_ptr->tpr = DIAG_INT_FREE_INT_HANDLE;
    free_irq(icom_adapter_ptr->pci_dev->irq, (void *)icom_adapter_ptr);
    icom_adapter_ptr->resources &= ~HAVE_INT_HANDLE;

    icom_adapter_ptr->tpr = DIAG_INT_END;
    return 0; 
} 

/*******************************/
/* Diagnostic Instruction Test */
/*******************************/
static u32 __init diag_inst(struct icom_adapter *icom_adapter_ptr)
{

    u32           temp_32;
    u32           rc;
    u8            i;
    u32           exp_int1, exp_int2;
    u8            done;
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];
    dma_addr_t	dma_addr_pci;
    int		addr1, addr2, addr3, addr4;

    /* make sure the all ints are cleared before we start */
    icom_adapter_ptr->tpr = DIAG_INST_CLEAR_INTS;
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8004)); 
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8024));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8024)); 
    }
    else {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x4004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x4004)); 
    }

    /* save off irq and request irq line */
    icom_adapter_ptr->tpr = DIAG_INST_GET_INT_HANDLE;
    if ((rc = request_irq(icom_adapter_ptr->pci_dev->irq, diag_int_handler, SA_INTERRUPT |
                          SA_SHIRQ, ICOM_DRIVER_NAME, (void *)icom_adapter_ptr))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Instruction Test. request_irq call failed.\n", /* error message */
                   DIAG_INST_GET_IRQ_LINE_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f;
    }
    else
        icom_adapter_ptr->resources |= HAVE_INT_HANDLE;

    /* load all active procs */
    icom_adapter_ptr->tpr = DIAG_INST_LOAD_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            diag_port_load(i,
                           &icom_adapter_ptr->port_info[i],
                           icom_adapter_ptr->base_addr,
                           toinstdiag, 
                           sizeof(toinstdiag));

    /* clear int regs */
    icom_adapter_ptr->tpr = DIAG_INST_CLEAR_INT_VARS;
    icom_adapter_ptr->diag_int_reset1 = 0;
    icom_adapter_ptr->diag_int_reset2 = 0;

    /* get storage to DMA to and from. Need 8 bytes for each port */
    icom_adapter_ptr->malloc_addr_1 =
        (u32 *)pci_alloc_consistent(icom_adapter_ptr->pci_dev,
                                    4096, &dma_addr_pci);

    if (!icom_adapter_ptr->malloc_addr_1)
        return -ENOMEM;

    icom_adapter_ptr->resources |= HAVE_MALLOC_1; /* indicate we have storage from malloc */

    icom_adapter_ptr->malloc_addr_1_pci = dma_addr_pci;
    addr1 = (int)dma_addr_pci;
    addr2 = addr1+8;
    addr3 = addr1+16;
    addr4 = addr1+24;

    writel(addr1,(void*)(icom_adapter_ptr->base_addr+8)); 
    writel(addr2,(void*)(icom_adapter_ptr->base_addr+8+0x2000)); 
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2){ 
        writel(addr3,(void*)(icom_adapter_ptr->base_addr+8+0x4000)); 
        writel(addr4,(void*)(icom_adapter_ptr->base_addr+8+0x6000));
    }

    /* start all procs */
    icom_adapter_ptr->tpr = DIAG_INST_START_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            start_processor(&icom_adapter_ptr->port_info[i]);

    /* Start all tests */
    icom_adapter_ptr->tpr = DIAG_INST_START_TESTS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            writeb((0x01),(void*)(icom_adapter_ptr->base_addr+i*0x2000)); 

    /* determine what ints to look for */
    icom_adapter_ptr->tpr = DIAG_INST_DETERMINE_INTS_TO_CHECK_FOR;
    if ((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2) {
        exp_int1 = (u32)0x01000100;
        exp_int2 = 0;
    }
    else {
        if(icom_adapter_ptr->subsystem_id == FOUR_PORT_MODEL) {
            exp_int1 = 0x01000100;
            exp_int2 = 0x01000100;
        }
        else { /* must be a two port model */
            exp_int1 = 0x00000100;
            exp_int2 = 0x00000100;
        }
    }

    /* wait for all ints to set or for a second to pass */
    done = 0;
    temp_32 = 0;
    icom_adapter_ptr->tpr = DIAG_INST_WAIT_FOR_INTS;
    while((!done) && (temp_32 < 100)) {
        if((icom_adapter_ptr->diag_int_reset1 == exp_int1) &&
           (icom_adapter_ptr->diag_int_reset2 == exp_int2))
            done = 1;

        /**** wait 1 millisecond ****/
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/100); /* HZ = 1 second */
        temp_32++;
    }

    temp_32 = 0;


    icom_adapter_ptr->tpr = DIAG_INST_CHECK_FOR_INTS;
    if(!done) { /* failed */
        /* fail the appropriate port/s */
        rc = diag_check_ints(icom_adapter_ptr,
                             exp_int1,
                             exp_int2,
                             "Instruction Test. Int did not occur.\n",
                             DIAG_INST_WRONG_INT_FAIL);
        return -1;
    }

    /* stop all procs */
    icom_adapter_ptr->tpr = DIAG_INST_STOP_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            stop_processor(&icom_adapter_ptr->port_info[i]);

    /**** check the results data ****/
    icom_adapter_ptr->tpr = DIAG_INST_CHECK_RESULTS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE) {
            /* read the results word */
            temp_32 = readl((void*)(icom_adapter_ptr->base_addr+(i*0x2000)+4));

            /* check results word */
            if(temp_32 != 0) {
                error_data[0] = temp_32;

                diag_error(icom_adapter_ptr,	/* port to fail */
                           i,  /* IOA_FAILURE indicates an Adapter failure */
                           "icom: Instruction Test. Bad results data.\n", /* error message */
                           DIAG_INST_BAD_RESULTS_DATA_FAIL,		/* tpr */
                           error_ptr,		/* error data pointer */
                           1);	/* number of error words. */
                return 0x0000000f;
            }
        }


    /**** return int handler ****/
    icom_adapter_ptr->tpr = DIAG_INST_FREE_INT_HANDLE;
    free_irq(icom_adapter_ptr->pci_dev->irq, (void *)icom_adapter_ptr);
    icom_adapter_ptr->resources &= ~HAVE_INT_HANDLE;

    /**** return storage from malloc ****/
    pci_free_consistent(icom_adapter_ptr->pci_dev,
                        4096, icom_adapter_ptr->malloc_addr_1,
                        icom_adapter_ptr->malloc_addr_1_pci);
    icom_adapter_ptr->resources &= ~HAVE_MALLOC_1;

    icom_adapter_ptr->tpr = DIAG_INST_END;
    return 0; 
} 

/****************************/
/* Diagnostic Register Test */
/****************************/
static u32 __init diag_reg(struct icom_adapter *icom_adapter_ptr)
{

    u32           temp_32;
    u32           rc;
    u8            i;
    u32           exp_int1, exp_int2;
    u8            done;
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];
    u32		itteration;
    u8		port;


    /* make sure the all ints are cleared before we start */
    icom_adapter_ptr->tpr = DIAG_REG_CLEAR_INTS+itteration;
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8004)); 
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8024));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8024)); 
    }
    else{
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x4004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x4004)); 
    }

    /* save off irq and request irq line */
    icom_adapter_ptr->tpr = DIAG_REG_GET_INT_HANDLE+itteration;
    if ((rc = request_irq(icom_adapter_ptr->pci_dev->irq, diag_int_handler, SA_INTERRUPT |
                          SA_SHIRQ, ICOM_DRIVER_NAME, (void *)icom_adapter_ptr))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: Register Test. request_irq call failed.\n", /* error message */
                   DIAG_REG_GET_IRQ_LINE_FAIL+itteration,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f;
    }
    else
        icom_adapter_ptr->resources |= HAVE_INT_HANDLE;

    /* Do this test twice, once for each pico code load */
    for(itteration=0;itteration<200;itteration+=100) {
        /* load all active procs */
        icom_adapter_ptr->tpr = DIAG_REG_LOAD_PROCS+itteration;
        for(i=0;i<icom_adapter_ptr->numb_ports;i++) {
            if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE) {
                if(itteration == 0) { /* if first time through this code */
                    diag_port_load(i,
                                   &icom_adapter_ptr->port_info[i],
                                   icom_adapter_ptr->base_addr,
                                   toregdiag, 
                                   sizeof(toregdiag));
                }
                else {
                    diag_port_load(i,
                                   &icom_adapter_ptr->port_info[i],
                                   icom_adapter_ptr->base_addr,
                                   toreg2diag, 
                                   sizeof(toreg2diag));
                }
            }
        }


        /* clear int regs */
        icom_adapter_ptr->tpr = DIAG_REG_CLEAR_INT_VARS+itteration;
        icom_adapter_ptr->diag_int_reset1 = 0;
        icom_adapter_ptr->diag_int_reset2 = 0;

        /* if the port number is 1 or 3, put 1 in the port field(offset 8) */
        /* The pico code needs to know if it is the second port of two.    */
        for(i=0;i<icom_adapter_ptr->numb_ports;i++) {
            port = icom_adapter_ptr->port_info[i].port;
            if((port == 1) || (port == 3))
                writel(1,(void*)(icom_adapter_ptr->base_addr+(port*0x2000)+8));
        }

        /* start all procs */
        icom_adapter_ptr->tpr = DIAG_REG_START_PROCS+itteration;
        for(i=0;i<icom_adapter_ptr->numb_ports;i++)
            if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
                start_processor(&icom_adapter_ptr->port_info[i]);

        /**** send the command to start the pico code ****/
        for(i=0;i<icom_adapter_ptr->numb_ports;i++)
            if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
                writeb(1,(void*)(icom_adapter_ptr->base_addr+(i*0x2000)));


        /**** determine what ints to look for ****/
        icom_adapter_ptr->tpr = DIAG_REG_DETERMINE_INTS_TO_CHECK_FOR+itteration;
        if ((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2) {
            exp_int1 = (u32)0x01000100;
            exp_int2 = 0;
        }
        else {
            if(icom_adapter_ptr->subsystem_id == FOUR_PORT_MODEL) {
                exp_int1 = 0x01000100;
                exp_int2 = 0x01000100;
            }
            else  { /* must be a two port model */
                exp_int1 = 0x00000100;
                exp_int2 = 0x00000100;
            }
        }

        /**** wait for all ints to set or for a second to pass ****/
        done = 0;
        temp_32 = 0;
        icom_adapter_ptr->tpr = DIAG_REG_WAIT_FOR_INTS+itteration;
        while((!done) && (temp_32 < 100)) {
            if((icom_adapter_ptr->diag_int_reset1 == exp_int1) &&
               (icom_adapter_ptr->diag_int_reset2 == exp_int2))
                done = 1;

            /**** wait 1 millisecond ****/
            current->state = TASK_UNINTERRUPTIBLE;
            schedule_timeout(HZ/100); /* HZ = 1 second */
            temp_32++;
        }

        temp_32 = 0;

        icom_adapter_ptr->tpr = DIAG_REG_CHECK_FOR_INTS+itteration;

        if(!done) { /* failed */ 
            /* fail the appropriate port/s */
            rc = diag_check_ints(icom_adapter_ptr,
                                 exp_int1,
                                 exp_int2,
                                 "Register Test. Int did not occur.\n",
                                 DIAG_REG_WRONG_INT_FAIL);
            return -1;
        }

        /**** stop all procs ****/
        icom_adapter_ptr->tpr = DIAG_REG_STOP_PROCS+itteration;
        for(i=0;i<icom_adapter_ptr->numb_ports;i++)
            if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
                stop_processor(&icom_adapter_ptr->port_info[i]);

        /**** check the results data ****/
        icom_adapter_ptr->tpr = DIAG_REG_CHECK_RESULTS+itteration;
        for(i=0;i<icom_adapter_ptr->numb_ports;i++)
            if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE) {
                /* read the results word */
                temp_32 = readl((void*)(icom_adapter_ptr->base_addr+(i*0x2000)+4));

                /* check results word */
                if(temp_32 != 0) {
                    error_data[0] = temp_32;

                    diag_error(icom_adapter_ptr,	/* port to fail */
                               i,  /* IOA_FAILURE indicates an Adapter failure */
                               "icom: Register Test. Bad results data.\n", /* error message */
                               DIAG_REG_BAD_RESULTS_DATA_FAIL+itteration,		/* tpr */
                               error_ptr,		/* error data pointer */
                               1);	/* number of error words. */
                    return 0x0000000f;
                }
            }
    } /* end of "for each pico code load" */

    /**** return int handler ****/
    icom_adapter_ptr->tpr = DIAG_REG_FREE_INT_HANDLE+itteration;
    free_irq(icom_adapter_ptr->pci_dev->irq, (void *)icom_adapter_ptr);
    icom_adapter_ptr->resources &= ~HAVE_INT_HANDLE;

    icom_adapter_ptr->tpr = DIAG_REG_END+itteration;

    return 0; 
} 

/***************************************/
/* Diagnostic RVX Status Register Test */
/***************************************/
static u32 __init diag_rvx_sts(struct icom_adapter *icom_adapter_ptr)
{

    u32           temp_32;
    u32           rc;
    u8            i;
    u32           exp_int1, exp_int2;
    u8            done;
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];

    /* make sure the all ints are cleared before we start */
    icom_adapter_ptr->tpr = DIAG_RVXSTS_CLEAR_INTS;
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8004)); 
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8024));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8024)); 
    }
    else {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x4004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x4004)); 
    }

    /* save off irq and request irq line */
    icom_adapter_ptr->tpr = DIAG_RVXSTS_GET_INT_HANDLE;
    if ((rc = request_irq(icom_adapter_ptr->pci_dev->irq, diag_int_handler, SA_INTERRUPT |
                          SA_SHIRQ, ICOM_DRIVER_NAME, (void *)icom_adapter_ptr))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: RVX Status Register Test. request_irq call failed.\n", /* error message */
                   DIAG_RVXSTS_GET_IRQ_LINE_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f;
    }
    else
        icom_adapter_ptr->resources |= HAVE_INT_HANDLE;

    /* load all active procs */
    icom_adapter_ptr->tpr = DIAG_RVXSTS_LOAD_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            diag_port_load(i,
                           &icom_adapter_ptr->port_info[i],
                           icom_adapter_ptr->base_addr,
                           tostsdiag, 
                           sizeof(tostsdiag));

    /* clear int regs */
    icom_adapter_ptr->tpr = DIAG_RVXSTS_CLEAR_INT_VARS;
    icom_adapter_ptr->diag_int_reset1 = 0;
    icom_adapter_ptr->diag_int_reset2 = 0;

    /* start all procs */
    icom_adapter_ptr->tpr = DIAG_RVXSTS_START_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            start_processor(&icom_adapter_ptr->port_info[i]);

    /* determine what ints to look for */
    icom_adapter_ptr->tpr = DIAG_RVXSTS_DETERMINE_INTS_TO_CHECK_FOR;
    if ((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2) {
        exp_int1 = 0x01000100;
        exp_int2 = 0;
    }
    else {
        if(icom_adapter_ptr->subsystem_id == FOUR_PORT_MODEL) {
            exp_int1 = 0x01000100;
            exp_int2 = 0x01000100;
        }
        else { /* must be a two port model */
            exp_int1 = 0x00000100;
            exp_int2 = 0x00000100;
        }
    }

    /* wait for all ints to set or for a second to pass */
    done = 0;
    temp_32 = 0;
    icom_adapter_ptr->tpr = DIAG_RVXSTS_WAIT_FOR_INTS;
    while((!done) && (temp_32 < 100)) {
        if((icom_adapter_ptr->diag_int_reset1 == exp_int1) &&
           (icom_adapter_ptr->diag_int_reset2 == exp_int2))
            done = 1;

        /**** wait 1 millisecond ****/
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/100); /* HZ = 1 second */
        temp_32++;
    }

    temp_32 = 0;

    icom_adapter_ptr->tpr = DIAG_RVXSTS_CHECK_FOR_INTS;
    if(!done) { /* failed */
        /* fail the appropriate port/s */
        rc = diag_check_ints(icom_adapter_ptr,
                             exp_int1,
                             exp_int2,
                             "Instruction Test. Int did not occur.\n",
                             DIAG_RVXSTS_WRONG_INT_FAIL);
        return -1;
    }

    /* stop all procs */
    icom_adapter_ptr->tpr = DIAG_RVXSTS_STOP_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            stop_processor(&icom_adapter_ptr->port_info[i]);

    /**** check the results data ****/
    icom_adapter_ptr->tpr = DIAG_RVXSTS_CHECK_RESULTS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE) {
            /* read the results word */
            temp_32 = readl((void*)(icom_adapter_ptr->base_addr+(i*0x2000)+4));

            /* check results word */
            if(temp_32 != 0xAAAAAAAA) { /* AAAAAAAA is passing */
                error_data[0] = temp_32;

                diag_error(icom_adapter_ptr,	/* port to fail */
                           i,  /* IOA_FAILURE indicates an Adapter failure */
                           "icom: Instruction Test. Bad results data.\n", /* error message */
                           DIAG_RVXSTS_BAD_RESULTS_DATA_FAIL,		/* tpr */
                           error_ptr,		/* error data pointer */
                           1);	/* number of error words. */
                return 0x0000000f;
            }
        }


    /**** return int handler ****/
    icom_adapter_ptr->tpr = DIAG_RVXSTS_FREE_INT_HANDLE;
    free_irq(icom_adapter_ptr->pci_dev->irq, (void *)icom_adapter_ptr);
    icom_adapter_ptr->resources &= ~HAVE_INT_HANDLE;

    icom_adapter_ptr->tpr = DIAG_RVXSTS_END;
    return 0; 
}

/*************************************/
/* Diagnostic RVX Internal Wrap Test */
/*************************************/
static u32 __init diag_rvx_internal_wrap(struct icom_adapter *icom_adapter_ptr)
{

    u32           temp_32;
    u32           rc;
    u8            i;
    u32           exp_int1, exp_int2;
    u8            done;
    u32		error_data[NUM_ERROR_ENTRIES];
    u32		*error_ptr = &error_data[0];

    /* make sure the all ints are cleared before we start */
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_CLEAR_INTS;
    if ((icom_adapter_ptr->version | ADAPTER_V2) == ADAPTER_V2) {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8004)); 
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x8024));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x8024)); 
    }
    else {
        temp_32 = readl((void*)(icom_adapter_ptr->base_addr+0x4004));
        writel(temp_32,(void*)(icom_adapter_ptr->base_addr+0x4004)); 
    }

    /* save off irq and request irq line */
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_GET_INT_HANDLE;
    if ((rc = request_irq(icom_adapter_ptr->pci_dev->irq, diag_int_handler, SA_INTERRUPT |
                          SA_SHIRQ, ICOM_DRIVER_NAME, (void *)icom_adapter_ptr))) {
        error_data[0] = rc;

        diag_error(icom_adapter_ptr,
                   IOA_FAILURE,  /* IOA_FAILURE indicates an Adapter failure */
                   "icom: RVX Internal Wrap Test. request_irq call failed.\n", /* error message */
                   DIAG_RVXINTWRP_GET_IRQ_LINE_FAIL,		/* tpr */
                   error_ptr,		/* error data pointer */
                   1);	/* number of error words. */
        return 0x0000000f;
    }
    else
        icom_adapter_ptr->resources |= HAVE_INT_HANDLE;

    /* load all active procs */
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_LOAD_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            diag_port_load(i,
                           &icom_adapter_ptr->port_info[i],
                           icom_adapter_ptr->base_addr,
                           towrapdiag, 
                           sizeof(towrapdiag));

    /* clear int regs */
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_CLEAR_INT_VARS;
    icom_adapter_ptr->diag_int_reset1 = 0;
    icom_adapter_ptr->diag_int_reset2 = 0;

    /* Load the Transmit data */
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE) {
            writel(0x11000000,(void*)(icom_adapter_ptr->base_addr+ 0x8  +(0x2000*i)));
            writel(0x22000000,(void*)(icom_adapter_ptr->base_addr+ 0xc  +(0x2000*i)));
            writel(0x33000000,(void*)(icom_adapter_ptr->base_addr+ 0x10 +(0x2000*i)));
            writel(0x44000000,(void*)(icom_adapter_ptr->base_addr+ 0x14 +(0x2000*i)));
        }

    /* start all procs */
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_START_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            start_processor(&icom_adapter_ptr->port_info[i]);

    /* determine what ints to look for */
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_DETERMINE_INTS_TO_CHECK_FOR;
    if ((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2) {
        exp_int1 = 0x01000100;
        exp_int2 = 0;
    }
    else {
        if(icom_adapter_ptr->subsystem_id == FOUR_PORT_MODEL) {
            exp_int1 = 0x01000100;
            exp_int2 = 0x01000100;
        }
        else { /* must be a two port model */
            exp_int1 = 0x00000100;
            exp_int2 = 0x00000100;
        }
    }

    /* wait for all ints to set or for a second to pass */
    done = 0;
    temp_32 = 0;
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_WAIT_FOR_INTS;
    while((!done) && (temp_32 < 100)) {
        if((icom_adapter_ptr->diag_int_reset1 == exp_int1) &&
           (icom_adapter_ptr->diag_int_reset2 == exp_int2))
            done = 1;

        /**** wait 1 millisecond ****/
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(HZ/100); /* HZ = 1 second */
        temp_32++;
    }

    temp_32 = 0;


    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_CHECK_FOR_INTS;
    if(!done) { /* failed */
        /* fail the appropriate port/s */
        rc = diag_check_ints(icom_adapter_ptr,
                             exp_int1,
                             exp_int2,
                             "Instruction Test. Int did not occur.\n",
                             DIAG_RVXINTWRP_WRONG_INT_FAIL);
        return -1;
    }

    /* stop all procs */
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_STOP_PROCS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            stop_processor(&icom_adapter_ptr->port_info[i]);

    /**** check the results data ****/
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_CHECK_RESULTS;
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE) {
            /* read the results word */
            temp_32 = readl((void*)(icom_adapter_ptr->base_addr+(i*0x2000)+4));

            /* check results word */
            if(temp_32 != 0xAAAAAAAA) { /* AAAAAAAA is passing */
                error_data[0] = temp_32;

                diag_error(icom_adapter_ptr,	/* port to fail */
                           i,  /* IOA_FAILURE indicates an Adapter failure */
                           "icom: Instruction Test. Bad results data.\n", /* error message */
                           DIAG_RVXINTWRP_BAD_RESULTS_DATA_FAIL,		/* tpr */
                           error_ptr,		/* error data pointer */
                           1);	/* number of error words. */
                return 0x0000000f;
            }
        }


    /**** return int handler ****/
    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_FREE_INT_HANDLE;
    free_irq(icom_adapter_ptr->pci_dev->irq, (void *)icom_adapter_ptr);
    icom_adapter_ptr->resources &= ~HAVE_INT_HANDLE;

    icom_adapter_ptr->tpr = DIAG_RVXINTWRP_END;
    return 0; 
} 

/**************************/
/* Main Diagnostic Driver */
/**************************/
static u32 __init diag_main(struct icom_adapter *icom_adapter_ptr)
{
    u32           rc = 0;
    int		i;
#ifdef icom_debug
    struct LocationDataStruct *p_location_data = NULL;
#endif

    icom_adapter_ptr->tpr = DIAG_MAIN_START;
#ifdef icom_debug
    printk(KERN_INFO"icom: Diagnostic started. ********\n");
#ifdef CONFIG_PPC_ISERIES
    p_location_data = iSeries_GetLocationData(icom_adapter_ptr->pci_dev);

    if (p_location_data != NULL) {
        printk(KERN_INFO"icom: Frame ID: %d Card Position: %s.\n",p_location_data->FrameId, p_location_data->CardLocation);
        kfree(p_location_data);
    }
#endif
#endif

    icom_adapter_ptr->tpr = DIAG_MAIN_INIT;

    diag_init(icom_adapter_ptr);


    icom_adapter_ptr->tpr = DIAG_MAIN_BIST;
    /* run BIST test */
    if(rc==0) {
#ifdef icom_debug
        printk(KERN_INFO"icom: Diagnostic BIST test.\n");
#endif
        rc = diag_bist(icom_adapter_ptr);
        if(rc!=0)
        {
            printk(KERN_ERR"icom: Diagnostic BIST test FAILED.\n");
        }
    }

    icom_adapter_ptr->tpr = DIAG_MAIN_MEMORY;
    /* run memory test */
    if(rc==0) {
#ifdef icom_debug
        printk(KERN_INFO"icom: Diagnostic memory.\n");
#endif
        rc = diag_memory(icom_adapter_ptr);
        if(rc!=0)
        {
            printk(KERN_ERR"icom: Diagnostic memory test FAILED.\n");
        }
    }


    icom_adapter_ptr->tpr = DIAG_MAIN_INTERRUPT;
    /* run interrupt test */
    if(rc==0) {
#ifdef icom_debug
        printk(KERN_INFO"icom: Diagnostic interrupt test.\n");
#endif
        rc = diag_int(icom_adapter_ptr);
        if(rc!=0) {
            printk(KERN_ERR"icom: Diagnostic interrupt test FAILED.\n");
        }
    }

    icom_adapter_ptr->tpr = DIAG_MAIN_INSTRUCTION;
    /* run instruction test */
    if(rc==0) {
#ifdef icom_debug
        printk(KERN_INFO"icom: Diagnostic instruction test.\n");
#endif
        rc = diag_inst(icom_adapter_ptr);
        if(rc!=0) {
            printk(KERN_ERR"icom: Diagnostic instruction test FAILED.\n");
        }
    }

    icom_adapter_ptr->tpr = DIAG_MAIN_REGISTER;
    /* run register test */
    if(rc==0) {
#ifdef icom_debug
        printk(KERN_INFO"icom: Diagnostic register test.\n");
#endif
        rc = diag_reg(icom_adapter_ptr);
        if(rc!=0) {
            printk(KERN_ERR"icom: Diagnostic register test FAILED.\n");
        }
    }


    icom_adapter_ptr->tpr = DIAG_MAIN_RVX_STATUS;
    /* run RVX status register test */
    if(rc==0) {
#ifdef icom_debug
        printk(KERN_INFO"icom: Diagnostic RVX Status Register test.\n");
#endif
        rc = diag_rvx_sts(icom_adapter_ptr);
        if(rc!=0) {
            printk(KERN_ERR"icom: Diagnostic RVX Status register test FAILED.\n");
        }
    }

    icom_adapter_ptr->tpr = DIAG_MAIN_RVX_INTERNAL_WRAP;
    /* run RVX internal wrap test */
    if(rc==0) {
#ifdef icom_debug
        printk(KERN_ERR"icom: Diagnostic RVX Internal Wrap test.\n");
#endif
        rc = diag_rvx_internal_wrap(icom_adapter_ptr);
        if(rc!=0) {
            printk(KERN_ERR"icom: Diagnostic RVX Internal Wrap test FAILED.\n");
        }
    }

    /* make sure all procs are stoped */
    for(i=0;i<icom_adapter_ptr->numb_ports;i++)
        if(icom_adapter_ptr->port_info[i].status == ICOM_PORT_ACTIVE)
            stop_processor(&icom_adapter_ptr->port_info[i]);

    /* return the resources */
    diag_return_resources(icom_adapter_ptr);

    /* mask all interrupts */
    if ((icom_adapter_ptr->version | ADAPTER_V2) != ADAPTER_V2)
        writel(0xFFFFFFFF,(void*)(icom_adapter_ptr->base_addr+0x4008));
    else  {
        writel(0xFFFF7FFF,(void*)(icom_adapter_ptr->base_addr+0x8008));
        writel(0xFFFFFFFF,(void*)(icom_adapter_ptr->base_addr+0x8028));
    }

    icom_adapter_ptr->tpr = DIAG_MAIN_END;

    if(rc==0) {
#ifdef icom_debug
        printk(KERN_INFO"icom: Diagnostic Completed Successfully ********\n");
#endif

        /* set the TPR and error data to zero for all ports */
        for(i=0;i<icom_adapter_ptr->numb_ports;i++) {
            icom_adapter_ptr->port_info[i].passed_diags = 1; /* true */
        }
    }
    else {
#ifdef icom_debug
        printk(KERN_ERR"icom: Diagnostic FAILED ********\n");
#endif
    }


    return rc;
}

static void __init diag_return_resources(struct icom_adapter *icom_adapter_ptr)
{
    /* make sure all resources are returned before leaving */
    if(icom_adapter_ptr->resources & HAVE_INT_HANDLE) {
        free_irq(icom_adapter_ptr->pci_dev->irq, (void *)icom_adapter_ptr);
        icom_adapter_ptr->resources &= ~HAVE_INT_HANDLE; /* clear resource bit */
    }

    if(icom_adapter_ptr->resources & HAVE_MALLOC_1) {
        pci_free_consistent(icom_adapter_ptr->pci_dev,
                            4096, icom_adapter_ptr->malloc_addr_1,
                            icom_adapter_ptr->malloc_addr_1_pci);
        icom_adapter_ptr->resources &= ~HAVE_MALLOC_1; /* clear resource bit */
    }
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
