/*
 * $Revision: 1.1 $
 * $Source: /home/master/uaep/driver/linux/paep.h,v $
 * $Date: 2002/02/21 17:00:41 $
 *
 * $CN{$ 
 * 
 * Copyright (c) 1999-2001 Accelerated Encryption Processing Ltd. (AEP)
 * Bray Business Park, Southern Cross Route, Bray, Co. Wicklow, Ireland.
 * All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of AEP Ltd. nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * 
 * $}CN$
 */

#ifndef __PAEP_H__
#define __PAEP_H__

/* define this to make all debugs print all the time */
/* #define KERN_DEBUG KERN_CRIT */

/* define this to add some instrumentation */
/*#define INSTRUMENT 1*/


/* DEBUG STUFF ONLY !! */
/*-------------------------------*/

/*  #define DUMMY_AEP_READ_PROCMEM */ 
/*  #define DUMMY_READ_AEP */         
/*  #define DUMMY_WRITE_AEP */        
/*  #define DUMMY_OPEN_AEP */
/*  #define DUMMY_RELEASE_AEP */      
/*  #define DUMMY_AEP_INTERRUPT_HANDLER */  
/*  #define DUMMY_AEP_INIT_DEVICES */
#define AEP_HAS_PROC_ENTRY

/*-------------------------------*/



#if !defined(CONFIG_PCI)
#error "CONFIG_PCI must be set for this PCI AEP(tm) accelerator"
#endif

#if !defined(MODULE)
/* support for non modular device driver is no longer provided in the code,  
   as this would require a kernel recompilation, which AEP installation CD  
   does not support anyway. Also, we don't want to modify the customer's kernels
   and provide support for it */ 
#error "MODULE not defined, I guess you don't want to recompile the entire kernel !"
#endif

#if !defined(__OPTIMIZE__) 
#error "This driver must be compiled with -O flag"
#endif

#if !defined(__KERNEL__) 
#error "Please use kernel source code to compile this driver"
#endif



#ifdef AEP_DEBUG
#define	assert(expression) { \
	if (!(expression)) { \
		(void)panic( \
			"assertion \"%s\" failed: file \"%s\", line %d\n", \
			#expression, \
			__FILE__, __LINE__); \
	} \
}
#else /*AEP_DEBUG*/
#define assert(expr)
#endif/*AEP_DEBUG*/


#ifdef AEP_DEBUG
/* useful dor debugging */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else/*AEP_DEBUG*/
#define DPRINTK(fmt, args...)
#endif/*AEP_DEBUG*/

#define PCI_AEP_ERROR_HEADER "PCI AEP driver: Error"
#define ERR_PRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, PCI_AEP_ERROR_HEADER , ## args)

#define PCI_AEP_WARNING_HEADER "PCI AEP driver: Warning"
#define WARN_PRINTK(fmt, args...) printk(KERN_WARNING "%s: " fmt, PCI_AEP_WARNING_HEADER , ## args)

#ifdef AEP_DEBUG
/* useful for debugging, separates DPRINTK messages, which makes it easier to read */
#define BANNER() printk(KERN_DEBUG "%s%s%s", "----------------------", __FUNCTION__, "----------------------\n")
#else
#define BANNER()
#endif
/**************************** AEP SPECIFIC DEFINE's ************************************/

#define AEP_PCI_VENDOR          0x8086
#define AEP_PCI_ID              0x1200
#define AEP_SUBSYS_VENDOR		0x172a
#define AEP_SUBSYS_ID			0x0
#define AEP_DEVICE_NAME         "paep"

#define AEP_DRIVER_NAME AEP_DEVICE_NAME 

/*Device memory region sizes */
#define AEP_CONFIG_SIZE         0x80
#define AEP_MEM_SIZE            32 * 1024 * 1024
/*#define AEP_MEM_SIZE          0x2000*/

#define AEP_MAXCARDS_NB         8              /* max number of cards supported by driver */
#define AEP_DMA_BUFFER_SIZE     128 * 1024     /* make this a power of two */

/* Device Registers */
#define PCI_OUT_INT_STATUS	0x30
#define PCI_OUT_INT_MASK        0x34
#define I20_INB_FIFO 		0x40
#define MAILBOX_0		0x50
#define MAILBOX_1		0x54
#define MAILBOX_2		0x58
#define MAILBOX_3		0x5C
#define DOORBELL		0x60
#define DOORBELL_SETUP          0x64

#define BAD_MESSAGE		0xffffffff

/* Interrupt bits */
#define PCI_DOORBELL_INT        0x00000004
#define PCI_OUTPOST_INT         0x00000008

/*Define DOORBELL codes*/
#define SIGNAL_DRIVER		0x01
#define DMA_WRITE_COMPLETE	0x02
#define DMA_WRITE_NO_SPACE	0x04
#define DMA_WRITE_ERROR		0x08
#define DMA_READ_COMPLETE	0x20	
#define DMA_READ_ERROR		0x80

/* 5 seconds for timeout */
#define IO_TIMEOUT 5 

#define DMA_DEVICE_WRITE 	1
#define DMA_DEVICE_READ 	2

#define LIMIT                   (PAGE_SIZE-80) /* the max we can print */

#define	DEV_OPEN		0x00000001
#define DMA_DEV_BUSY		0x00000002
#define WAITING_FOR_READ_DATA   0x00000004
#define READ_DATA_AVAILABLE     0x00000008

#define RS_IDLE                 0x00000001 /* nothing going on */
#define RS_DMA_ACTIVE           0x00000002 /* waiting for completion */
#define RS_DMA_COMPLETE         0x00000003 /* complete but process not resumed */
#define RS_DEAD                 0x0000DEAD

#define WS_IDLE                 0x00010000
#define WS_DMA_ACTIVE           0x00020000
#define WS_DMA_COMPLETE         0x00030000
#define WS_DEAD                 0xDEAD0000

/***************************************************************************************/


/* Per-Unit Data */
typedef struct{

            /* entries in the /dev filesystem */ 
        devfs_handle_t dev_fs_handle;
        int major;
    
            /* various flags so we know what to release */
        int int_inuse;

            /* pointers to config space and memory space */
        void *config_space;
        void *mem_space;

            /* flags we don't want to have to share */
        volatile u32 last_read_dma;
        volatile u32 last_write_dma;

            /* various flags */
    
        volatile u32 flags; /* may be changed by ISR */
        volatile u32 read_state;
        volatile u32 write_state;

            /* Pointers to data buffer */
        char *read_dma_buf_p;
        char *write_dma_buf_p;

            /* various things passed up from the int handler */
        volatile u32   read_residual;
        volatile u32   write_fail_reason;

        struct semaphore aep_sem; /* critical section */
		struct semaphore read_sem;

        wait_queue_head_t drq; /* do read queue */
        wait_queue_head_t rq;
        wait_queue_head_t wq;
	wait_queue_head_t select_wq;

            /* various debugging and testing fields */
#ifdef INSTRUMENT

        u32 open_cnt;
        u32 open_busy_cnt;
        u32 close_cnt;

        u32 read_cnt;
        u32 read_dma_cnt;
        u32 read_busy_cnt;
        u32 read_byte_cnt;
        u32 write_cnt;
        u32 write_dma_cnt;
        u32 write_busy_cnt;
        u32 write_busy_cnt2;
        u32 write_byte_cnt;

        u32 interrupt_cnt;
        u32 SIGNAL_DRIVER_int_cnt;
        u32 DMA_WRITE_COMPLETE_int_cnt;
        u32 DMA_WRITE_NO_SPACE_int_cnt;
        u32 DMA_WRITE_ERROR_int_cnt;
        u32 DMA_READ_COMPLETE_int_cnt;
        u32 DMA_READ_ERROR_int_cnt;

#endif /* INSTRUMENT */

} paep_unit_info_t;


typedef struct {
        int command;
        int address;
        int length;

} DMA_msg_t;


#define read_pci_status(dev_p) readl(((paep_unit_info_t*)dev_p->driver_data)->config_space + PCI_OUT_INT_STATUS)
#define read_doorbell_status(dev_p) readl(((paep_unit_info_t*)dev_p->driver_data)->config_space + DOORBELL)
#define read_mailbox_status(dev_p) readl(((paep_unit_info_t*)dev_p->driver_data)->config_space + MAILBOX_0)

#define write_doorbell_status(value, dev_p) writel(value, ((paep_unit_info_t*)dev_p->driver_data)->config_space + DOORBELL)
#define acknowledge_irq(value, dev_p) write_doorbell_status(value, dev_p)

#define get_msg_ptr(dev_param_p) readl(dev_param_p->config_space + I20_INB_FIFO)
#define post_msg(msg_address, dev_param_p) writel(msg_address, dev_param_p->config_space + I20_INB_FIFO)

#define exit_if_invalid_msg_ptr(msg_address, dev_param_p) \
 if (targ_msg == BAD_MESSAGE) {\
 ERR_PRINTK("can't get a valid target message ptr\n");\
 up(&dev_param_p->aep_sem);\
 return -EIO;}\


#define exit_if_too_many_cards(count) \
 if (count >= AEP_MAXCARDS_NB){\
 ERR_PRINTK("Can't have more than %d accelerators with this driver (returning -ENOMEM)\n", AEP_MAXCARDS_NB);\
 return -ENOMEM;\
}


#define DPRINTK_MSG(msg_p) {\
 DPRINTK( "          Command: 0x%08X\n", msg_p->command); \
 DPRINTK( "          Address: 0x%08X\n", msg_p->address); \
 DPRINTK( "          Length:  0x%08X\n", msg_p->length); \
}


static void inline write_dma_msg(const DMA_msg_t *msg_p, paep_unit_info_t *dev_param_p, int target_address);
static void inline make_dma_msg(DMA_msg_t *msg_p, int command, int address, int length);


static ssize_t read_aep(struct file *file_p, char *buf_p, size_t count, loff_t *ppos_p);
static ssize_t write_aep(struct file *file_p, const char *buf_p, size_t count, loff_t *ppos_p);

static size_t poll_aep(struct file *file_p, poll_table *wait);

static int     open_aep(struct inode *inode_p, struct file *file_p);
static int     release_aep(struct inode *inode_p, struct file *file_p);
void           aep_interrupt_handler(int irq, void *dev_id_p, struct pt_regs *regs_p);
int            aep_read_procmem(char *buf_p, char **start_pp, off_t offset,
                                int len, int *unused_p, void* also_unused_p);

static int __init     aep_init_module(void);
static void __exit    aep_cleanup_module(void);

static void __devexit aep_remove_one_device(struct pci_dev *dev_p);
static int  __devinit aep_init_one_device (struct pci_dev *dev_p, const struct pci_device_id *unused_p);

#if 0 /* BROKEN STUFF */
static int  __devinit aep_init_all_devices (struct pci_dev *unused1, const struct pci_device_id *unused2);
static void __devexit aep_remove_all_devices(struct pci_dev *unused);
#endif


#endif /* __PAEP_H__ */
