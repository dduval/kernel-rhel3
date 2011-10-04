/*
 * NS16550 Serial Port (uart) debugging stuff.
 *
 * c 2001 PPC 64 Team, IBM Corp
 *
 * NOTE: I am trying to make this code avoid any static data references to
 *  simplify debugging early boot.  We'll see how that goes...
 *
 * To use this call udbg_init() first.  It will init the uart to 9600 8N1.
 * You may need to update the COM1 define if your uart is at a different addr.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#define WANT_PPCDBG_TAB /* Only defined here */
#include <asm/ppcdebug.h>
#include <asm/processor.h>
#include <asm/naca.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>

#if defined(CONFIG_ICOM)||defined(CONFIG_ICOM_MODULE)

#include <asm/io.h>		/* writeb */
#include <linux/mm.h>		/* gfp_kernel */
#include <linux/slab.h>		/* kfree */
#include <linux/serial.h>	/* struct async_icount */
#include <linux/icom_udbg.h>	/* icom structures */
#include <asm/iSeries/HvLpConfig.h>  /* getLps */

#include <linux/module.h> /* export_symbol */

void iCom_sercons_putchar(unsigned char);
char iCom_sercons_getchar(void);

char recvd[256];
int input_buf_loc = 0;
/* lock for access to the card. */
static spinlock_t serial_lock = SPIN_LOCK_UNLOCKED;

struct icom_adapter *icom_adapter_info;

#define ICOM_STARTED 0x1
static spinlock_t udbg_state_lock = SPIN_LOCK_UNLOCKED;
volatile unsigned int udbg_state=0;

/* #define ICOM_TRACE */

#ifdef ICOM_TRACE
void TRACE(struct icom_port *icom_port_ptr, u32 trace_pt,
	   u32 trace_data);
#endif
#endif /* ICOM */

struct NS16550 {
	/* this struct must be packed */
	unsigned char rbr;  /* 0 */
	unsigned char ier;  /* 1 */
	unsigned char fcr;  /* 2 */
	unsigned char lcr;  /* 3 */
	unsigned char mcr;  /* 4 */
	unsigned char lsr;  /* 5 */
	unsigned char msr;  /* 6 */
	unsigned char scr;  /* 7 */
};

#define thr rbr
#define iir fcr
#define dll rbr
#define dlm ier
#define dlab lcr

#define LSR_DR   0x01  /* Data ready */
#define LSR_OE   0x02  /* Overrun */
#define LSR_PE   0x04  /* Parity error */
#define LSR_FE   0x08  /* Framing error */
#define LSR_BI   0x10  /* Break */
#define LSR_THRE 0x20  /* Xmit holding register empty */
#define LSR_TEMT 0x40  /* Xmitter empty */
#define LSR_ERR  0x80  /* Error */

volatile struct NS16550 *udbg_comport;

void
udbg_init_uart(void *comport)
{
	if (comport) {
		udbg_comport = (struct NS16550 *)comport;
		udbg_comport->lcr = 0x00; eieio();
		udbg_comport->ier = 0xFF; eieio();
		udbg_comport->ier = 0x00; eieio();
		udbg_comport->lcr = 0x80; eieio();	/* Access baud rate */
		udbg_comport->dll = 12;   eieio();	/* 1 = 115200,  2 = 57600, 3 = 38400, 12 = 9600 baud */
		udbg_comport->dlm = 0;    eieio();	/* dll >> 8 which should be zero for fast rates; */
		udbg_comport->lcr = 0x03; eieio();	/* 8 data, 1 stop, no parity */
		udbg_comport->mcr = 0x03; eieio();	/* RTS/DTR */
		udbg_comport->fcr = 0x07; eieio();	/* Clear & enable FIFOs */
	}
}

/* '1' to indicate started. '0' to indicate stopped. */
int
iCom_udbg_started(int s) {
#if defined(CONFIG_ICOM)||defined(CONFIG_ICOM_MODULE)
    spin_lock(&udbg_state_lock);
    if (s==1) {
	udbg_state |= ICOM_STARTED;
	printk("iCom_udbg_started\n");
    }
    else if (s==0) {
	udbg_state &= ~ICOM_STARTED;
	printk("iCom_udbg_ stopped\n");
    }
    spin_unlock(&udbg_state_lock);
    return (udbg_state & ICOM_STARTED);
#else
    return 0;
#endif  /* icom */
}

void
udbg_putc(unsigned char c)
{
#if defined(CONFIG_ICOM)||defined(CONFIG_ICOM_MODULE)
    if (iCom_udbg_started(-1)) {
	if (iCom_sercons_putchar)
	    iCom_sercons_putchar(c);
	return;
    }
#endif
	if ( udbg_comport ) {
		while ((udbg_comport->lsr & LSR_THRE) == 0)
			/* wait for idle */;
		udbg_comport->thr = c; eieio();
		if (c == '\n') {
			/* Also put a CR.  This is for convenience. */
			while ((udbg_comport->lsr & LSR_THRE) == 0)
				/* wait for idle */;
			udbg_comport->thr = '\r'; eieio();
		}
	} else if (systemcfg->platform == PLATFORM_ISERIES_LPAR) {
		/* ToDo: switch this via ppc_md */
		printk("%c", c);
	}
}

int udbg_getc_poll(void)
{
        if (iCom_udbg_started(-1)) {
	   return -1;                    /* wms - this might be a problem */
        }
	if (udbg_comport) {
		if ((udbg_comport->lsr & LSR_DR) != 0)
			return udbg_comport->rbr;
		else
			return -1;
	}
	return -1;
}

unsigned char
udbg_getc(void)
{
#if defined(CONFIG_ICOM)||defined(CONFIG_ICOM_MODULE)
        if (iCom_udbg_started(-1)) {
	    if (iCom_sercons_getchar) 
		return iCom_sercons_getchar();
        }
#endif
	if ( udbg_comport ) {
		while ((udbg_comport->lsr & LSR_DR) == 0)
			/* wait for char */;
		return udbg_comport->rbr;
	}
	return -1;
}

void
udbg_puts(const char *s)
{
	if (ppc_md.udbg_putc) {
		char c;

		if (s && *s != '\0') {
			while ((c = *s++) != '\0')
				ppc_md.udbg_putc(c);
		}
	} else {
		printk("%s", s);
	}
}

int
udbg_write(const char *s, int n)
{
	int remain = n;
	char c;
	if (!ppc_md.udbg_putc)
		return 0;
	if ( s && *s != '\0' ) {
		while ( (( c = *s++ ) != '\0') && (remain-- > 0)) {
			ppc_md.udbg_putc(c);
		}
	}
	return n - remain;
}

int
udbg_read(char *buf, int buflen) {
	char c, *p = buf;
	int i;
	if (!ppc_md.udbg_putc)
		for (;;);	/* stop here for cpuctl */
	for (i = 0; i < buflen; ++i) {
		do {
			c = ppc_md.udbg_getc();
		} while (c == 0x11 || c == 0x13);
		if (c == -1)	/* error occurred or no getc possible*/
			break;
		*p++ = c;
	}
	return i;
}

void
udbg_console_write(struct console *con, const char *s, unsigned int n)
{
	udbg_write(s, n);
}

void
udbg_puthex(unsigned long val)
{
	int i, nibbles = sizeof(val)*2;
	unsigned char buf[sizeof(val)*2+1];
	for (i = nibbles-1;  i >= 0;  i--) {
		buf[i] = (val & 0xf) + '0';
		if (buf[i] > '9')
		    buf[i] += ('a'-'0'-10);
		val >>= 4;
	}
	buf[nibbles] = '\0';
	udbg_puts(buf);
}

void
udbg_printSP(const char *s)
{
	if (systemcfg->platform == PLATFORM_PSERIES) {
		unsigned long sp;
		asm("mr %0,1" : "=r" (sp) :);
		if (s)
			udbg_puts(s);
		udbg_puthex(sp);
	}
}

void
udbg_printf(const char *fmt, ...)
{
	unsigned char buf[256];

	va_list args;
	va_start(args, fmt);

	vsprintf(buf, fmt, args);
	udbg_puts(buf);

	va_end(args);
}

/* Special print used by PPCDBG() macro */
void
udbg_ppcdbg(unsigned long debug_flags, const char *fmt, ...)
{
	unsigned long active_debugs = debug_flags & naca->debug_switch;

	if ( active_debugs ) {
		va_list ap;
		unsigned char buf[256];
		unsigned long i, len = 0;

		for(i=0; i < PPCDBG_NUM_FLAGS ;i++) {
			if (((1U << i) & active_debugs) && 
			    trace_names[i]) {
				len += strlen(trace_names[i]); 
				udbg_puts(trace_names[i]);
				break;
			}
		}
		sprintf(buf, " [%s]: ", current->comm);
		len += strlen(buf); 
		udbg_puts(buf);

		while(len < 18) {
			udbg_puts(" ");
			len++;
		}

		va_start(ap, fmt);
		vsprintf(buf, fmt, ap);
		udbg_puts(buf);
		
		va_end(ap);
	}
}

unsigned long
udbg_ifdebug(unsigned long flags)
{
	return (flags & naca->debug_switch);
}

#if defined(CONFIG_ICOM)||defined(CONFIG_ICOM_MODULE)
/* Udbg/Icom interface functions below. */

/* the following sercons functions provide basic card operation for using the serial port as a debugger. they assume that the debugging is being done on the first port on the first card initialized.  */
void clear_recv_buffers(struct icom_port *icom_port_ptr) {
    /* */
    memset((char *)icom_port_ptr->recv_buf, 0, RCV_BUFF_SZ);
    icom_port_ptr->statStg->rcv[0].flags = 0;
    icom_port_ptr->statStg->rcv[0].leLength = 0;
    icom_port_ptr->statStg->rcv[0].WorkingLength =
      (unsigned short int)cpu_to_le16(RCV_BUFF_SZ);

    memset((char *)icom_port_ptr->recv_buf+2048, 0, RCV_BUFF_SZ);
    icom_port_ptr->statStg->rcv[1].flags = 0;
    icom_port_ptr->statStg->rcv[1].leLength = 0;
    icom_port_ptr->statStg->rcv[1].WorkingLength =
      (unsigned short int)cpu_to_le16(RCV_BUFF_SZ);
}

/* notes: string parameter to mdm_poll function must be 256 chars long */
static int mdm_poll(struct icom_port *icom_port_ptr, char *string) {
    int status = 0;
    int loop_count = 0;
    unsigned char cmdReg;

   /* init string to null */
    memset(string, 0, 256);

   /* set DTR RTS up */
    writeb(0xC0,&icom_port_ptr->dram->osr);

   /* make sure we're in send/rcv mode */
    cmdReg = readb(&icom_port_ptr->dram->CmdReg);
    writeb(cmdReg | CMD_XMIT_RCV_ENABLE,&icom_port_ptr->dram->CmdReg);

   /* check for data on modem */
    while (!status && (loop_count++ < 50))
    {
	HvCallCfg_getLps();

	if(cpu_to_le16(icom_port_ptr->statStg->rcv[0].flags) & SA_FL_RCV_DONE)
	{
	    int length;
	    length = min(256,cpu_to_le16(icom_port_ptr->statStg->rcv[0].leLength));
	   /* copy received data to string */
	    strncat(string, (char *)icom_port_ptr->recv_buf,length);
	   /* reset the buffer */
	    memset((char *)icom_port_ptr->recv_buf, 0, RCV_BUFF_SZ);
	    icom_port_ptr->statStg->rcv[0].flags = 0;
	    icom_port_ptr->statStg->rcv[0].leLength = 0;
	    icom_port_ptr->statStg->rcv[0].WorkingLength = (unsigned short int)cpu_to_le16(RCV_BUFF_SZ);
	    status = 1;
	}
	else if(cpu_to_le16(icom_port_ptr->statStg->rcv[1].flags) & SA_FL_RCV_DONE)
	{
	   /* copy received data to string */
	    int length;
	    length = min(256,cpu_to_le16(icom_port_ptr->statStg->rcv[1].leLength));
	    strncat(string, (char *)icom_port_ptr->recv_buf + 2048,length);
	   /* reset the buffer */
	    memset((char *)icom_port_ptr->recv_buf+2048, 0, RCV_BUFF_SZ);
	    icom_port_ptr->statStg->rcv[1].flags = 0;
	    icom_port_ptr->statStg->rcv[1].leLength = 0;
	    icom_port_ptr->statStg->rcv[1].WorkingLength = (unsigned short int)cpu_to_le16(RCV_BUFF_SZ);
	    status=1;
	}
    }

    /* clear interrupts */
    writew(0x3FFF,(void *)icom_port_ptr->int_reg);

    return status;
}

static void mdm_send(struct icom_port *icom_port_ptr, char *mdm_cmnd,
		     int cmnd_length) {

    unsigned char     cmdReg;
    unsigned long int offset;
    char	tmp_byte;
    int index;

    if (0 == cmnd_length) {

    /* turn on DTR and RTS */
	writeb(0xC0,&icom_port_ptr->dram->osr);
	tmp_byte = readb(&(icom_port_ptr->dram->HDLCConfigReg));
	tmp_byte |= HDLC_HDW_FLOW;
	writeb(tmp_byte, &(icom_port_ptr->dram->HDLCConfigReg));

    /* initialize transmit and receive operations */
	offset = (unsigned long int)&icom_port_ptr->statStg->rcv[0] - (unsigned long int)icom_port_ptr->statStg;
	writel(icom_port_ptr->statStg_pci + offset,&icom_port_ptr->dram->RcvStatusAddr);
	icom_port_ptr->next_rcv = 0;
	icom_port_ptr->put_length = 0;
	*icom_port_ptr->xmitRestart = 0;
	writel(icom_port_ptr->xmitRestart_pci,&icom_port_ptr->dram->XmitStatusAddr);
	writeb(CMD_XMIT_RCV_ENABLE,&icom_port_ptr->dram->CmdReg);

    /* wait for CTS */
	for (index=0; index < 20; index++) {
	    current->state = TASK_INTERRUPTIBLE;
	    schedule_timeout(HZ/100); /* HZ = .01 second */
	    if (readb(&icom_port_ptr->dram->isr) & ICOM_CTS) break;
	}
	if (index == 20) printk("iCom:  WARNING CTS not up in 200ms for BATs\n");
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ); /* HZ = 1 second */
	return;
    }

  /* clear target receive buffers 1 and 2 */
    memset(icom_port_ptr->recv_buf,0,4096);

    memcpy(icom_port_ptr->xmit_buf, mdm_cmnd, cmnd_length);

    icom_port_ptr->statStg->xmit[0].flags = (unsigned short int)cpu_to_le16(SA_FLAGS_READY_TO_XMIT);
    icom_port_ptr->statStg->xmit[0].leLength = (unsigned short int)cpu_to_le16(cmnd_length);
    offset = (unsigned long int)&icom_port_ptr->statStg->xmit[0] - (unsigned long int)icom_port_ptr->statStg;
    *icom_port_ptr->xmitRestart = cpu_to_le32(icom_port_ptr->statStg_pci + offset);

    cmdReg = readb(&icom_port_ptr->dram->CmdReg);
    writeb(cmdReg | CMD_XMIT_RCV_ENABLE,&icom_port_ptr->dram->CmdReg);

    writeb(START_XMIT,&icom_port_ptr->dram->StartXmitCmd);
#ifdef ICOM_TRACE
    TRACE(icom_port_ptr,TRACE_WRITE_START,cmnd_length);
#endif
}

void mdm_check_send(struct icom_port *icom_port_ptr, char *mdm_cmnd,
		    int cmnd_length) {
    unsigned char status;
    int loops = 0;

    /* check other modem and see if it is ready	*/
    status = readb(&icom_port_ptr->dram->isr);

    /* spin for a few to let the char appear */
    while( 1 || !(status & ICOM_DSR) || 
	   !(status & ICOM_CTS) ) {
	status = readb(&icom_port_ptr->dram->isr);/* pause and check again */
	if(loops++ > 900){
	    break;
	}
	HvCallCfg_getLps();
    }
    mdm_send(icom_port_ptr, mdm_cmnd, cmnd_length);
}

/* basic setup to get the card working 
   Just set speed, character and parity */
void iCom_sercons_init(void) {
	int index;
	char config2 = ICOM_ACFG_DRIVE1;
	char config3, tmp_byte;
	extern char recvd[256];

	struct icom_port *icom_port_ptr = &(icom_adapter_info[0].port_info[0]);

	config2 |= ICOM_ACFG_8BPC; /*set 8 bits per char */
	config2 |= ICOM_ACFG_2STOP_BIT; /*set to 2 stop bits */
	/*config2 |= ICOM_ACFG_PARITY_ENAB; parity enabled */
	/*config2 |= ICOM_ACFG_PARITY_ODD; odd parity */
	
	/* set baud to 9600 */
	for(index = 0; index < BAUD_TABLE_LIMIT; index++){
		if (icom_acfg_baud[index] == 9600){
			config3 = index;
			break;
		}
	}

	writeb(config3, &(icom_port_ptr->dram->async_config3) );
  	writeb(config2, &(icom_port_ptr->dram->async_config2) );
  	tmp_byte = readb(&(icom_port_ptr->dram->HDLCConfigReg));
  	tmp_byte |= HDLC_PPP_PURE_ASYNC | HDLC_FF_FILL | HDLC_HDW_FLOW;
  	writeb(tmp_byte,&(icom_port_ptr->dram->HDLCConfigReg));
  	writeb(0x01, &(icom_port_ptr->dram->FlagFillIdleTimer)); /* 0.1 seconds */
  	writeb(0xFF, &(icom_port_ptr->dram->ier)); /* enable modem signal interrupts */

	writeb(CMD_RESTART, &(icom_port_ptr->dram->CmdReg));
	/* Looks like there is a timing window between prior writeb and the
             first mdm_send call.   Todo:  attempt to replace this for() with a readb(), should force the writeb operation to complete. */
	for(index = 0; index < 100; index++)
		;

	/* call mdm_send with cmd length 0 to take initialization path */
	mdm_send( &(icom_adapter_info[0].port_info[0]), NULL, 0);

	/* let udbg know card is OK to print to */
        iCom_udbg_started(1);
	recvd[0]=0;
}

char iCom_sercons_poll(void) {
	int status;
	char str[256];

	spin_lock(&serial_lock); /*lock */
	status = mdm_poll(&(icom_adapter_info[0].port_info[0]), str);
	spin_unlock(&serial_lock); /* lock */
	if(status)
		return str[0];
	return -1;
}

char iCom_sercons_getchar(void) {
	char c;

	spin_lock(&serial_lock); /*lock*/
	
	/* clear the input buffers  */
	/* clear_recv_buffers(&(icom_adapter_info[0].port_info[0])); */

	if (recvd[input_buf_loc] != 0){
		c = recvd[input_buf_loc];
		input_buf_loc++;
	} else {
		/* loop until something is in the buffers */
	    while( !(mdm_poll(&(icom_adapter_info[0].port_info[0]), recvd)) ){
		;
	    }
	    c = recvd[0];
	    input_buf_loc = 1;
	}
	
	spin_unlock(&serial_lock); /*lock*/
	
	return c;
}

void iCom_sercons_putchar(unsigned char c) {

	char ch = c;

	if(ch == -1)
		return;
	
	spin_lock(&serial_lock);
	
	/*if ch is a new line print a carriage return as well */
        if(ch == '\n' || ch =='\r') {
                ch = '\n';
                mdm_check_send( &(icom_adapter_info[0].port_info[0]), &ch, 1);
		ch = '\r';
                mdm_check_send( &(icom_adapter_info[0].port_info[0]), &ch, 1);
        } 
	else /* try to send directly to modem */
        	mdm_check_send( &(icom_adapter_info[0].port_info[0]), &ch, 1);

	spin_unlock(&serial_lock);
}

#ifdef ICOM_TRACE
void TRACE(struct icom_port *icom_port_ptr, u32 trace_pt,
	   u32 trace_data) {
    u32 *tp_start, *tp_end, **tp_next;

    if (trace_pt == TRACE_GET_MEM) {
	if (icom_port_ptr->trace_blk != 0) return;
	icom_port_ptr->trace_blk = kmalloc(TRACE_BLK_SZ,GFP_KERNEL);
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
#endif

EXPORT_SYMBOL(recvd);
EXPORT_SYMBOL(iCom_sercons_init);
EXPORT_SYMBOL(icom_adapter_info);

#endif /* ICOM */
