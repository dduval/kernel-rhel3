/*
 *  drivers/char/viocons.c
 *
 *  iSeries Virtual Terminal
 *
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *
 * (C) Copyright 2000, 2001, 2002, 2003 IBM Corporation
 *
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) anyu later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <asm/ioctls.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/sysrq.h>

#include "vio.h"

#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvCallEvent.h>
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/HvCall.h>
#include <asm/iSeries/iSeries_proc.h>

/* Check that the tty_driver_data actually points to our stuff
 */
#define VIOTTY_PARANOIA_CHECK 1
#define VIOTTY_MAGIC (0x0DCB)

static int debug;

static DECLARE_WAIT_QUEUE_HEAD(viocons_wait_queue);

#define VTTY_PORTS 10
#define VIOTTY_SERIAL_START 65

static u64 sndMsgSeq[VTTY_PORTS];
static u64 sndMsgAck[VTTY_PORTS];

static spinlock_t consolelock = SPIN_LOCK_UNLOCKED;
static spinlock_t consoleloglock = SPIN_LOCK_UNLOCKED;

/* This is a place where we handle the distribution of memory
 * for copy_from_user() calls.  We use VIO_MAX_SUBTYPES because it
 * seems as good a number as any.  The buffer_available array is to
 * help us determine which buffer to use.
 */
static void *viocons_cfu_buffer[VIO_MAX_SUBTYPES];
static atomic_t viocons_cfu_buffer_available[VIO_MAX_SUBTYPES];

#ifdef CONFIG_MAGIC_SYSRQ
static int vio_sysrq_pressed;
extern struct sysrq_ctls_struct sysrq_ctls;
#endif

/* THe structure of the events that flow between us and OS/400.  You can't
 * mess with this unless the OS/400 side changes too
 */
struct viocharlpevent {
	struct HvLpEvent event;
	u32 mReserved1;
	u16 mVersion;
	u16 mSubTypeRc;
	u8 virtualDevice;
	u8 immediateDataLen;
	u8 immediateData[VIOCHAR_MAX_DATA];
};

#define viochar_window (10)
#define viochar_highwatermark (3)

enum viocharsubtype {
	viocharopen = 0x0001,
	viocharclose = 0x0002,
	viochardata = 0x0003,
	viocharack = 0x0004,
	viocharconfig = 0x0005
};

enum viochar_rc {
	viochar_rc_ebusy = 1
};

/* When we get writes faster than we can send it to the partition,
 * buffer the data here.  There is one set of buffers for each virtual
 * port.
 * Note that bufferUsed is a bit map of used buffers.
 * It had better have enough bits to hold NUM_BUF
 * the bitops assume it is a multiple of unsigned long
 */
#define NUM_BUF (8)
#define OVERFLOW_SIZE VIOCHAR_MAX_DATA

static struct overflowBuffers {
	unsigned long bufferUsed;
	u8 *buffer[NUM_BUF];
	int bufferBytes[NUM_BUF];
	int curbuf;
	int bufferOverflow;
	int overflowMessage;
} overflow[VTTY_PORTS];

static void initDataEvent(struct viocharlpevent *viochar, HvLpIndex lp);

static int viocons_init_cfu_buffer(void);
static void *viocons_get_cfu_buffer(void);

static struct tty_driver viotty_driver;
static struct tty_driver viottyS_driver;
static int viotty_refcount;

static struct tty_struct *viotty_table[VTTY_PORTS];
static struct tty_struct *viottyS_table[VTTY_PORTS];
static struct termios *viotty_termios[VTTY_PORTS];
static struct termios *viottyS_termios[VTTY_PORTS];
static struct termios *viotty_termios_locked[VTTY_PORTS];
static struct termios *viottyS_termios_locked[VTTY_PORTS];

char viocons_hvlog_buffer[256];

void hvlog(char *fmt, ...)
{
	int i;
	unsigned long flags;
	va_list args;

	spin_lock_irqsave(&consoleloglock, flags);
	va_start(args, fmt);
	i = vsprintf(viocons_hvlog_buffer, fmt, args);
	va_end(args);
	HvCall_writeLogBuffer(viocons_hvlog_buffer, i);
	HvCall_writeLogBuffer("\r", 1);
	spin_unlock_irqrestore(&consoleloglock, flags);

}

void hvlogOutput( const char *buf, int count )
{
	unsigned long flags;
	int begin;
	int index;
	char cr;

	cr = '\r';
	begin = 0;
	spin_lock_irqsave(&consoleloglock, flags);
	for( index = 0; index < count; ++index ) {
		if( buf[index] == 0x0a ) {
			/* Start right after the last 0x0a or at the zeroth
			 * array position and output the number of characters
			 * including the newline.
			 */
			HvCall_writeLogBuffer(&buf[begin], index-begin+1);
			begin = index+1;
			HvCall_writeLogBuffer(&cr, 1);
		}
	}
	if(index-begin > 0) {
		HvCall_writeLogBuffer(&buf[begin], index-begin);
	}
	index = 0;
	begin = 0;

	spin_unlock_irqrestore(&consoleloglock, flags);
}


/* Our port information.  We store a pointer to one entry in the
 * tty_driver_data
 */
static struct port_info_tag {
	int magic;
	struct tty_struct *tty;
	HvLpIndex lp;
	u8 vcons;
	u8 port;
} port_info[VTTY_PORTS];

/* Make sure we're pointing to a valid port_info structure.  Shamelessly
 * plagerized from serial.c
 */
static inline int viotty_paranoia_check(struct port_info_tag *pi,
					kdev_t device, const char *routine)
{
#ifdef VIOTTY_PARANOIA_CHECK
	static const char *badmagic =
	    "\n\rWarning: bad magic number for port_info struct (%s) in %s.";
	static const char *badinfo =
	    "\n\rWarning: null port_info for (%s) in %s.";

	if (!pi) {
		hvlog(badinfo, kdevname(device), routine);
		return 1;
	}
	if (pi->magic != VIOTTY_MAGIC) {
		hvlog(badmagic, kdevname(device), routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * Handle reads from the proc file system.  Right now we just dump the
 * state of the first TTY
 */
static int proc_read(char *buf, char **start, off_t offset,
		     int blen, int *eof, void *data)
{
	int len = 0;
	struct tty_struct *tty = viotty_table[0];
	struct termios *termios;
	if (tty == NULL) {
		len += sprintf(buf + len, "no tty\n");
		*eof = 1;
		return len;
	}

	len +=
	    sprintf(buf + len,
		    "tty info: COOK_OUT %ld COOK_IN %ld, NO_WRITE_SPLIT %ld\n",
		    tty->flags & TTY_HW_COOK_OUT,
		    tty->flags & TTY_HW_COOK_IN,
		    tty->flags & TTY_NO_WRITE_SPLIT);

	termios = tty->termios;
	if (termios == NULL) {
		len += sprintf(buf + len, "no termios\n");
		*eof = 1;
		return len;
	}
	len += sprintf(buf + len, "INTR_CHAR     %2.2x\n", INTR_CHAR(tty));
	len += sprintf(buf + len, "QUIT_CHAR     %2.2x\n", QUIT_CHAR(tty));
	len +=
	    sprintf(buf + len, "ERASE_CHAR    %2.2x\n", ERASE_CHAR(tty));
	len += sprintf(buf + len, "KILL_CHAR     %2.2x\n", KILL_CHAR(tty));
	len += sprintf(buf + len, "EOF_CHAR      %2.2x\n", EOF_CHAR(tty));
	len += sprintf(buf + len, "TIME_CHAR     %2.2x\n", TIME_CHAR(tty));
	len += sprintf(buf + len, "MIN_CHAR      %2.2x\n", MIN_CHAR(tty));
	len += sprintf(buf + len, "SWTC_CHAR     %2.2x\n", SWTC_CHAR(tty));
	len +=
	    sprintf(buf + len, "START_CHAR    %2.2x\n", START_CHAR(tty));
	len += sprintf(buf + len, "STOP_CHAR     %2.2x\n", STOP_CHAR(tty));
	len += sprintf(buf + len, "SUSP_CHAR     %2.2x\n", SUSP_CHAR(tty));
	len += sprintf(buf + len, "EOL_CHAR      %2.2x\n", EOL_CHAR(tty));
	len +=
	    sprintf(buf + len, "REPRINT_CHAR  %2.2x\n", REPRINT_CHAR(tty));
	len +=
	    sprintf(buf + len, "DISCARD_CHAR  %2.2x\n", DISCARD_CHAR(tty));
	len +=
	    sprintf(buf + len, "WERASE_CHAR   %2.2x\n", WERASE_CHAR(tty));
	len +=
	    sprintf(buf + len, "LNEXT_CHAR    %2.2x\n", LNEXT_CHAR(tty));
	len += sprintf(buf + len, "EOL2_CHAR     %2.2x\n", EOL2_CHAR(tty));

	len += sprintf(buf + len, "I_IGNBRK      %4.4x\n", I_IGNBRK(tty));
	len += sprintf(buf + len, "I_BRKINT      %4.4x\n", I_BRKINT(tty));
	len += sprintf(buf + len, "I_IGNPAR      %4.4x\n", I_IGNPAR(tty));
	len += sprintf(buf + len, "I_PARMRK      %4.4x\n", I_PARMRK(tty));
	len += sprintf(buf + len, "I_INPCK       %4.4x\n", I_INPCK(tty));
	len += sprintf(buf + len, "I_ISTRIP      %4.4x\n", I_ISTRIP(tty));
	len += sprintf(buf + len, "I_INLCR       %4.4x\n", I_INLCR(tty));
	len += sprintf(buf + len, "I_IGNCR       %4.4x\n", I_IGNCR(tty));
	len += sprintf(buf + len, "I_ICRNL       %4.4x\n", I_ICRNL(tty));
	len += sprintf(buf + len, "I_IUCLC       %4.4x\n", I_IUCLC(tty));
	len += sprintf(buf + len, "I_IXON        %4.4x\n", I_IXON(tty));
	len += sprintf(buf + len, "I_IXANY       %4.4x\n", I_IXANY(tty));
	len += sprintf(buf + len, "I_IXOFF       %4.4x\n", I_IXOFF(tty));
	len += sprintf(buf + len, "I_IMAXBEL     %4.4x\n", I_IMAXBEL(tty));

	len += sprintf(buf + len, "O_OPOST       %4.4x\n", O_OPOST(tty));
	len += sprintf(buf + len, "O_OLCUC       %4.4x\n", O_OLCUC(tty));
	len += sprintf(buf + len, "O_ONLCR       %4.4x\n", O_ONLCR(tty));
	len += sprintf(buf + len, "O_OCRNL       %4.4x\n", O_OCRNL(tty));
	len += sprintf(buf + len, "O_ONOCR       %4.4x\n", O_ONOCR(tty));
	len += sprintf(buf + len, "O_ONLRET      %4.4x\n", O_ONLRET(tty));
	len += sprintf(buf + len, "O_OFILL       %4.4x\n", O_OFILL(tty));
	len += sprintf(buf + len, "O_OFDEL       %4.4x\n", O_OFDEL(tty));
	len += sprintf(buf + len, "O_NLDLY       %4.4x\n", O_NLDLY(tty));
	len += sprintf(buf + len, "O_CRDLY       %4.4x\n", O_CRDLY(tty));
	len += sprintf(buf + len, "O_TABDLY      %4.4x\n", O_TABDLY(tty));
	len += sprintf(buf + len, "O_BSDLY       %4.4x\n", O_BSDLY(tty));
	len += sprintf(buf + len, "O_VTDLY       %4.4x\n", O_VTDLY(tty));
	len += sprintf(buf + len, "O_FFDLY       %4.4x\n", O_FFDLY(tty));

	len += sprintf(buf + len, "C_BAUD        %4.4x\n", C_BAUD(tty));
	len += sprintf(buf + len, "C_CSIZE       %4.4x\n", C_CSIZE(tty));
	len += sprintf(buf + len, "C_CSTOPB      %4.4x\n", C_CSTOPB(tty));
	len += sprintf(buf + len, "C_CREAD       %4.4x\n", C_CREAD(tty));
	len += sprintf(buf + len, "C_PARENB      %4.4x\n", C_PARENB(tty));
	len += sprintf(buf + len, "C_PARODD      %4.4x\n", C_PARODD(tty));
	len += sprintf(buf + len, "C_HUPCL       %4.4x\n", C_HUPCL(tty));
	len += sprintf(buf + len, "C_CLOCAL      %4.4x\n", C_CLOCAL(tty));
	len += sprintf(buf + len, "C_CRTSCTS     %4.4x\n", C_CRTSCTS(tty));

	len += sprintf(buf + len, "L_ISIG        %4.4x\n", L_ISIG(tty));
	len += sprintf(buf + len, "L_ICANON      %4.4x\n", L_ICANON(tty));
	len += sprintf(buf + len, "L_XCASE       %4.4x\n", L_XCASE(tty));
	len += sprintf(buf + len, "L_ECHO        %4.4x\n", L_ECHO(tty));
	len += sprintf(buf + len, "L_ECHOE       %4.4x\n", L_ECHOE(tty));
	len += sprintf(buf + len, "L_ECHOK       %4.4x\n", L_ECHOK(tty));
	len += sprintf(buf + len, "L_ECHONL      %4.4x\n", L_ECHONL(tty));
	len += sprintf(buf + len, "L_NOFLSH      %4.4x\n", L_NOFLSH(tty));
	len += sprintf(buf + len, "L_TOSTOP      %4.4x\n", L_TOSTOP(tty));
	len += sprintf(buf + len, "L_ECHOCTL     %4.4x\n", L_ECHOCTL(tty));
	len += sprintf(buf + len, "L_ECHOPRT     %4.4x\n", L_ECHOPRT(tty));
	len += sprintf(buf + len, "L_ECHOKE      %4.4x\n", L_ECHOKE(tty));
	len += sprintf(buf + len, "L_FLUSHO      %4.4x\n", L_FLUSHO(tty));
	len += sprintf(buf + len, "L_PENDIN      %4.4x\n", L_PENDIN(tty));
	len += sprintf(buf + len, "L_IEXTEN      %4.4x\n", L_IEXTEN(tty));

	*eof = 1;
	return len;
}

/*
 * Handle writes to our proc file system.  Right now just turns on and off
 * our debug flag
 */
static int proc_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	if (count) {
		if (buffer[0] == '1') {
			printk(KERN_INFO_VIO "viocons: debugging on\n");
			debug = 1;
		} else {
			printk(KERN_INFO_VIO "viocons: debugging off\n");
			debug = 0;
		}
	}
	return count;
}

/*
 * setup our proc file system entries
 */
void viocons_proc_init(struct proc_dir_entry *iSeries_proc)
{
	struct proc_dir_entry *ent;
	ent =
	    create_proc_entry("viocons", S_IFREG | S_IRUSR, iSeries_proc);
	if (!ent)
		return;
	ent->nlink = 1;
	ent->data = NULL;
	ent->read_proc = proc_read;
	ent->write_proc = proc_write;
	ent->owner = THIS_MODULE;
}

/*
 * clean up our proc file system entries
 */
void viocons_proc_delete(struct proc_dir_entry *iSeries_proc)
{
	remove_proc_entry("viocons", iSeries_proc);
}

/*
 * This function should ONLY be called once from viocons_init2
 */
static int viocons_init_cfu_buffer( )
{
	int i;

	if (viocons_cfu_buffer[0] == NULL) {
		if (VIO_MAX_SUBTYPES <= 16) {
			viocons_cfu_buffer[0] = (void *) get_free_page(GFP_KERNEL);
			if (viocons_cfu_buffer[0] == NULL) {
				hvlog("\n\rviocons: get_free_page() for cfu_buffer FAILED.");
				return -ENOMEM;
			} else {
				/* We can fit sixteen 256 byte characters in each page
				 * of memory (4096).  This routine aligns the boundaries
				 * of the 256 byte cfu buffers incrementally along the space
				 * of the page we allocated earlier.  Start at index 1 because
				 * we've already done index 0 when we fetched the free page.
				 */
				for(i=1;i<VIO_MAX_SUBTYPES; i++) {
					viocons_cfu_buffer[i] = viocons_cfu_buffer[i-1] + 256;
					atomic_set(&viocons_cfu_buffer_available[i], 1);
				}
			}
		} else {
			hvlog("\n\rviocons: VIO_MAX_SUBTYPES > 16. Need more space for cfu buffer.");
			return -ENOMEM;
		}
	}
	return 0;
}

static void *viocons_get_cfu_buffer()
{
	int i;

	/* Grab the first available buffer.  It doesn't matter if we
	 * are interrupted during this array traversal as long as we
	 * get an available space.
	 */
	for(i = 0; i < VIO_MAX_SUBTYPES; i++) {
		if( atomic_dec_if_positive(&viocons_cfu_buffer_available[i]) == 0 ) {
			return viocons_cfu_buffer[i];
		}
	}
	hvlog("\n\rviocons: viocons_get_cfu_buffer : no free buffers found");
	return NULL;
}

static void viocons_free_cfu_buffer( void *buffer )
{
	int i;

	/* Cycle through the cfu_buffer array and see which position
	 * in the array matches the buffer pointer parameter.  This
	 * is the index of the event_buffer_available array that we
	 * want to reset to available.
	 */
	for(i = 0; i < VIO_MAX_SUBTYPES; i++) {
		if( viocons_cfu_buffer[i] == buffer ) {
			if (atomic_read(&viocons_cfu_buffer_available[i]) != 0) {
				hvlog("\n\rviocons: WARNING : returning unallocated cfu buffer.");
				return;
			}
			atomic_set(&viocons_cfu_buffer_available[i], 1);
			return;
		}
	}
	hvlog("\n\rviocons: viocons_free_cfu_buffer : buffer pointer not found in list.");
}

/*
 * Add data to our pending-send buffers.
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.  hvlog can be
 * used to log to the hypervisor buffer
 */
static int bufferAdd(u8 port, char *buf, size_t len)
{
	size_t bleft;
	size_t curlen;
	char *curbuf;
	int nextbuf;
	struct overflowBuffers *pov = &overflow[port];

	curbuf = buf;
	bleft = len;

	while (bleft > 0) {
		/* If there is no space left in the current buffer, we have
		 * filled everything up, so return.  If we filled the previous
		 * buffer we would already have moved to the next one.
		 */
		if (pov->bufferBytes[pov->curbuf] == OVERFLOW_SIZE) {
			hvlog ("\n\rviocons: No overflow buffer available for memcpy().\n");
			pov->bufferOverflow++;
			pov->overflowMessage = 1;
			break;
		}

		/*
		 * Turn on the "used" bit for this buffer.  If it's already on,
		 * that's fine.
		 */
		set_bit(pov->curbuf, &pov->bufferUsed);

		/*
		 * See if this buffer has been allocated.  If not, allocate it.
		 */
		if (pov->buffer[pov->curbuf] == NULL) {
			pov->buffer[pov->curbuf] = kmalloc(OVERFLOW_SIZE, GFP_ATOMIC);
			if (pov->buffer[pov->curbuf] == NULL) {
				hvlog("\n\rviocons: kmalloc failed allocating spaces for buffer %d.",pov->curbuf);
				break;
			}
		}

		/* Figure out how much we can copy into this buffer. */
		if (bleft < (OVERFLOW_SIZE - pov->bufferBytes[pov->curbuf]))
			curlen = bleft;
		else
			curlen = OVERFLOW_SIZE - pov->bufferBytes[pov->curbuf];

		/* Copy the data into the buffer. */
		memcpy(pov->buffer[pov->curbuf] +
			pov->bufferBytes[pov->curbuf], curbuf,
			curlen);

		pov->bufferBytes[pov->curbuf] += curlen;
		curbuf += curlen;
		bleft -= curlen;

		/*
		 * Now see if we've filled this buffer.  If not then
		 * we'll try to use it again later.  If we've filled it
		 * up then we'll advance the curbuf to the next in the
		 * circular queue.
		 */
		if (pov->bufferBytes[pov->curbuf] == OVERFLOW_SIZE) {
			nextbuf = (pov->curbuf + 1) % NUM_BUF;
			/*
			 * Move to the next buffer if it hasn't been used yet
			 */
			if (test_bit(nextbuf, &pov->bufferUsed) == 0) {
				pov->curbuf = nextbuf;
			}
		}
	}
	return len - bleft;
}

/* Send pending data
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.  hvlog can be
 * used to log to the hypervisor buffer
 */
void sendBuffers(u8 port, HvLpIndex lp)
{
	HvLpEvent_Rc hvrc;
	int nextbuf;
	struct viocharlpevent *viochar;
	unsigned long flags;
	struct overflowBuffers *pov = &overflow[port];

	spin_lock_irqsave(&consolelock, flags);

	viochar = (struct viocharlpevent *)
	    vio_get_event_buffer(viomajorsubtype_chario);

	/* Make sure we got a buffer */
	if (viochar == NULL) {
		hvlog("\n\rviocons: Can't get viochar buffer in sendBuffers().");
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

	if (pov->bufferUsed == 0) {
		hvlog("\n\rviocons: in sendbuffers(), but no buffers used.\n");
		vio_free_event_buffer(viomajorsubtype_chario, viochar);
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

	/*
	 * curbuf points to the buffer we're filling.  We want to start sending AFTER
	 * this one.
	 */
	nextbuf = (pov->curbuf + 1) % NUM_BUF;

	/*
	 * Loop until we find a buffer with the bufferUsed bit on
	 */
	while (test_bit(nextbuf, &pov->bufferUsed) == 0)
		nextbuf = (nextbuf + 1) % NUM_BUF;

	initDataEvent(viochar, lp);

	/*
	 * While we have buffers with data, and our send window is open, send them
	 */
	while ((test_bit(nextbuf, &pov->bufferUsed)) &&
	       ((sndMsgSeq[port] - sndMsgAck[port]) < viochar_window)) {
		viochar->immediateDataLen = pov->bufferBytes[nextbuf];
		viochar->event.xCorrelationToken = sndMsgSeq[port]++;
		viochar->event.xSizeMinus1 =
			offsetof(struct viocharlpevent, immediateData) +
				viochar->immediateDataLen;

		memcpy(viochar->immediateData, pov->buffer[nextbuf],
		       viochar->immediateDataLen);

		hvrc = HvCallEvent_signalLpEvent(&viochar->event);
		if (hvrc) {
			/*
			 * MUST unlock the spinlock before doing a printk
			 */
			vio_free_event_buffer(viomajorsubtype_chario,
					      viochar);
			spin_unlock_irqrestore(&consolelock, flags);

			printk(KERN_WARNING_VIO
			       "console error sending event! return code %d\n",
			       (int) hvrc);
			return;
		}

		/*
		 * clear the bufferUsed bit, zero the number of bytes in this buffer,
		 * and move to the next buffer
		 */
		clear_bit(nextbuf, &pov->bufferUsed);
		pov->bufferBytes[nextbuf] = 0;
		nextbuf = (nextbuf + 1) % NUM_BUF;
	}

	/*
	 * If we have emptied all the buffers, start at 0 again.
	 * this will re-use any allocated buffers
	 */
	if (pov->bufferUsed == 0) {
		pov->curbuf = 0;

		if (pov->overflowMessage)
			pov->overflowMessage = 0;

		if (port_info[port].tty) {
			tty_wakeup(port_info[port].tty);
		}
	}

	vio_free_event_buffer(viomajorsubtype_chario, viochar);
	spin_unlock_irqrestore(&consolelock, flags);

}

/* Our internal writer.  Gets called both from the console device and
 * the tty device.  the tty pointer will be NULL if called from the console.
 * Return total number of bytes "written".
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.  hvlog can be
 * used to log to the hypervisor buffer
 */
static int internal_write(HvLpIndex lp, u8 port, const char *buf,
			  size_t len, struct viocharlpevent *viochar)
{
	HvLpEvent_Rc hvrc;
	size_t bleft;
	size_t curlen;
	const char *curbuf;
	unsigned long flags;
	int copy_needed = (viochar == NULL);

	/* Writes to the hvlog of inbound data are now done prior to
	 * calling internal_write() since internal_write() is only called in
	 * the event that an lp event path is active, which isn't the case for
	 * logging attempts prior to console initialization.
	 */

	/*
	 * If there is already data queued for this port, send it prior to
	 * attempting to send any new data.
	 */
	if (overflow[port].bufferUsed)
		sendBuffers(port, lp);

	spin_lock_irqsave(&consolelock, flags);

	/* If the internal_write() was passed a pointer to a
	 * viocharlpevent then we don't need to allocate a new one
	 * (this is the case where we are internal_writing user space
	 * data).  If we aren't writing user space data then we need
	 * to get an event from viopath.
	 */
	if (copy_needed) {

		/* This one is fetched from the viopath data structure */
		viochar = (struct viocharlpevent *)
			vio_get_event_buffer(viomajorsubtype_chario);

		/* Make sure we got a buffer */
		if (viochar == NULL) {
			spin_unlock_irqrestore(&consolelock, flags);
			hvlog("\n\rviocons: Can't get viochar buffer in internal_write().");
			return -EAGAIN;
		}

		initDataEvent(viochar, lp);
	}

	curbuf = buf;
	bleft = len;

	while ((bleft > 0) &&
	       (overflow[port].bufferUsed == 0) &&
	       ((sndMsgSeq[port] - sndMsgAck[port]) < viochar_window)) {

		if (bleft > VIOCHAR_MAX_DATA)
			curlen = VIOCHAR_MAX_DATA;
		else
			curlen = bleft;

		viochar->event.xCorrelationToken = sndMsgSeq[port]++;

		if (copy_needed) {
			memcpy(viochar->immediateData, curbuf, curlen);
			viochar->immediateDataLen = curlen;
		}

		viochar->event.xSizeMinus1 = offsetof(struct viocharlpevent,
			immediateData) + viochar->immediateDataLen;

		hvrc = HvCallEvent_signalLpEvent(&viochar->event);
		if (hvrc) {
			spin_unlock_irqrestore(&consolelock, flags);
			if (copy_needed) {
				vio_free_event_buffer(viomajorsubtype_chario,
					viochar);
			}
			hvlog("viocons: error sending event! %d\n", (int) hvrc);
			return len - bleft;
		}

		curbuf += curlen;
		bleft -= curlen;
	}

	/*
	 * If we couldn't send it all, buffer as much of it as we can.
	 */
	if (bleft > 0) {
		bleft -= bufferAdd(port, curbuf, bleft);
	}

	/* Since we grabbed it from the viopath data structure, return it to the
	 * data structure
	 */
	if (copy_needed)
		vio_free_event_buffer(viomajorsubtype_chario, viochar);

	spin_unlock_irqrestore(&consolelock, flags);

	return len - bleft;
}

static int get_port_data(struct tty_struct *tty, HvLpIndex *lp, u8 *port)
{
	unsigned long flags;
	struct port_info_tag *pi = NULL;

	spin_lock_irqsave(&consolelock, flags);
	if (tty) {
		pi = (struct port_info_tag *) tty->driver_data;

		if (!pi
		    || viotty_paranoia_check(pi, tty->device,
					     "viotty_internal_write")) {
			spin_unlock_irqrestore(&consolelock, flags);
			return -ENODEV;
		}

		*lp = pi->lp;
		*port = pi->port;
	} else {
		/* If this is the console device, use the lp from the first port entry
		 */
		*port = 0;
		*lp = port_info[0].lp;
	}
	spin_unlock_irqrestore(&consolelock, flags);
	return 0;
}

/* Initialize the common fields in a charLpEvent */
static void initDataEvent(struct viocharlpevent *viochar, HvLpIndex lp)
{
	memset(viochar, 0x00, sizeof(struct viocharlpevent));

	viochar->event.xFlags.xValid = 1;
	viochar->event.xFlags.xFunction = HvLpEvent_Function_Int;
	viochar->event.xFlags.xAckInd = HvLpEvent_AckInd_NoAck;
	viochar->event.xFlags.xAckType = HvLpEvent_AckType_DeferredAck;
	viochar->event.xType = HvLpEvent_Type_VirtualIo;
	viochar->event.xSubtype = viomajorsubtype_chario | viochardata;
	viochar->event.xSourceLp = HvLpConfig_getLpIndex();
	viochar->event.xTargetLp = lp;
	viochar->event.xSizeMinus1 = sizeof(struct viocharlpevent);
	viochar->event.xSourceInstanceId = viopath_sourceinst(lp);
	viochar->event.xTargetInstanceId = viopath_targetinst(lp);
}

/* console device write
 */
static void viocons_write(struct console *co, const char *s,
			  unsigned count)
{
	int index;
	char charptr[1];
	int begin;
	HvLpIndex lp;
	u8 port;

	/* Check port data first because the target LP might be valid but
	 * simply not active, in which case we want to hvlog the output.
	 */
	if (get_port_data(NULL, &lp, &port)) {
		hvlog("\n\rviocons: in viocons_write unable to get port data.");
		return;
	}

	hvlogOutput(s,count);

	if(!viopath_isactive(lp)) {
		return;
	}

	/* 
	 * Any newline character (0x0a == '\n') found will cause a
	 * carriage return character to be emitted as well. 
	 */
	begin = 0;
	for (index = 0; index < count; index++) {
		if (s[index] == 0x0a) {
			/* 
			 * Newline found. Print everything up to and 
			 * including the newline
			 */
			internal_write(lp, port, &s[begin], index-begin+1, 0);
			begin = index + 1;
			/* Emit a carriage return as well */
			charptr[0] = '\r';
			internal_write(lp, port, charptr, 1, 0);
		}
	}

	/* If any characters left to write, write them now */
	if (index - begin > 0)
		internal_write(lp, port, &s[begin], index - begin, 0);
}

/* Work out a the device associate with this console
 */
static kdev_t viocons_device(struct console *c)
{
	return MKDEV(TTY_MAJOR, c->index + viotty_driver.minor_start);
}

/* Do console device setup
 */
static int __init viocons_setup(struct console *co, char *options)
{
	return 0;
}

/* console device I/O methods
 */
static struct console viocons = {
	name:"ttyS",
	write:viocons_write,
	device:viocons_device,
	setup:viocons_setup,
	flags:CON_PRINTBUFFER,
};

/* TTY Open method
 */
static int viotty_open(struct tty_struct *tty, struct file *filp)
{
	int port;
	unsigned long flags;
	MOD_INC_USE_COUNT;
	port = MINOR(tty->device) - tty->driver.minor_start;

	/* NOTE: in the event that a user space program attempts to open tty
	 * devices 2-x this viotty_open will succeed but will return an
	 * invalid tty device as far as this device driver is concerned.  We
	 * allow such behavior because many installers require additional tty
	 * devices even if we don't support them.  Further method invocations
	 * upon these invalid tty devices will simply fail gracefully.
	 */

	if (port >= VIOTTY_SERIAL_START)
		port -= VIOTTY_SERIAL_START;

	if ((port < 0) || (port >= VTTY_PORTS)) {
		MOD_DEC_USE_COUNT;
		return -ENODEV;
	}

	spin_lock_irqsave(&consolelock, flags);

	/*
	 * If some other TTY is already connected here, reject the open
	 */
	if ((port_info[port].tty) && (port_info[port].tty != tty)) {
		spin_unlock_irqrestore(&consolelock, flags);
		MOD_DEC_USE_COUNT;
		printk(KERN_INFO_VIO
		       "console attempt to open device twice from different ttys\n");
		return -EBUSY;
	}
	tty->driver_data = &port_info[port];
	port_info[port].tty = tty;
	spin_unlock_irqrestore(&consolelock, flags);

	return 0;
}

/* TTY Close method
 */
static void viotty_close(struct tty_struct *tty, struct file *filp)
{
	unsigned long flags;
	struct port_info_tag *pi = NULL;

	spin_lock_irqsave(&consolelock, flags);
	pi = (struct port_info_tag *) tty->driver_data;

	if (!pi || viotty_paranoia_check(pi, tty->device, "viotty_close")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

	if (atomic_read(&tty->count) == 1) {
		pi->tty = NULL;
	}

	spin_unlock_irqrestore(&consolelock, flags);

	MOD_DEC_USE_COUNT;
}

/* TTY Write method
 */
static int viotty_write(struct tty_struct *tty, int from_user,
			const unsigned char *buf, int count)
{
	struct viocharlpevent *viochar;
	int curlen;
	const char *curbuf = buf;
	int ret;
	int total = 0;
	HvLpIndex lp;
	u8 port;

	ret = get_port_data(tty, &lp, &port);
	if (ret) {
		hvlog("\n\rviocons: in viotty_write: no port data.");
		return -ENODEV;
	}

	hvlogOutput(buf,count);

	/* If the path to this LP is closed, don't bother doing anything more.
	 * just dump the data on the floor and return count.  For some reason some
	 * user level programs will attempt to probe available tty's and they'll
	 * attempt a viotty_write on an invalid port which maps to an invalid target
	 * lp.  If this is the case then ignore the viotty_write call and since
	 * the viopath isn't active to this partition return count.
	 */
	if (!viopath_isactive(lp)) {
		return count;
	}

	/* If the viotty_write is invoked from user space we want to do the
	 * copy_from_user() into an event buffer from the cfu buffer before
	 * internal_write() is called because internal_write may need to buffer
	 * data which will need to grab a spin_lock and we shouldn't
	 * copy_from_user() while holding a spin_lock.  Should internal_write()
	 * not need to buffer data then it'll just use the event we created here
	 * rather than checking one out from vio_get_event_buffer().
	 */
	if (from_user) {

		viochar = (struct viocharlpevent *) viocons_get_cfu_buffer();

		if (viochar == NULL)
			return -EAGAIN;

		initDataEvent(viochar, lp);

		while (count > 0) {
			if (count > VIOCHAR_MAX_DATA)
				curlen = VIOCHAR_MAX_DATA;
			else
				curlen = count;
			viochar->immediateDataLen = curlen;

			ret = copy_from_user(viochar->immediateData, curbuf, curlen);
			if (ret)
				break;

			ret = internal_write(lp, port, viochar->immediateData,
					viochar->immediateDataLen, viochar);
			total += ret;
			if (ret != curlen)
				break;
			count -= curlen;
			curbuf += curlen;
		}
		viocons_free_cfu_buffer(viochar);
	}
	else {
		total = internal_write(lp, port, buf, count, NULL);
	}
	return total;
}

/* TTY put_char method */
static void viotty_put_char(struct tty_struct *tty, unsigned char ch)
{
	HvLpIndex lp;
	u8 port;

	if (get_port_data(tty, &lp, &port)) {
		return;
	}

	/* This will append \r as well if the char is 0x0A ('\n') */
	if (port==0) {
		hvlogOutput(&ch,1);
	}

	if(!viopath_isactive(lp)) {
		return;
	}

	internal_write(lp, port, &ch, 1, 0);
}

/* TTY write_room method */
static int viotty_write_room(struct tty_struct *tty)
{
	int i;
	int room = 0;
	struct port_info_tag *pi = NULL;
	unsigned long flags;

	spin_lock_irqsave(&consolelock, flags);
	pi = (struct port_info_tag *) tty->driver_data;

	if (!pi
	    || viotty_paranoia_check(pi, tty->device,
				     "viotty_sendbuffers")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return 0;
	}

	/*
	 * If no buffers are used, return the max size.
	 */
	if (overflow[pi->port].bufferUsed == 0) {
		spin_unlock_irqrestore(&consolelock, flags);
		return VIOCHAR_MAX_DATA * NUM_BUF;
	}

	/* We retain the spinlock because we want to get an accurate
	 * count and it can change on us between each operation if we
	 * don't hold the spinlock.
	 */
	for (i = 0; ((i < NUM_BUF) && (room < VIOCHAR_MAX_DATA)); i++) {
		room +=
		    (OVERFLOW_SIZE - overflow[pi->port].bufferBytes[i]);
	}
	spin_unlock_irqrestore(&consolelock, flags);

	if (room > VIOCHAR_MAX_DATA)
		return VIOCHAR_MAX_DATA;
	else
		return room;
}

/* TTY chars_in_buffer_room method
 */
static int viotty_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}

static int viotty_ioctl(struct tty_struct *tty, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		/* the ioctls below read/set the flags usually shown in the leds */
		/* don't use them - they will go away without warning */
	case KDGETLED:
	case KDGKBLED:
		return put_user(0, (char *) arg);

	case KDSKBLED:
		return 0;
	}

	return n_tty_ioctl(tty, file, cmd, arg);
}

/* Handle an open charLpEvent.  Could be either interrupt or ack
 */
static void vioHandleOpenEvent(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *) event;
	u8 port = cevent->virtualDevice;
	int reject = 0;

	if (event->xFlags.xFunction == HvLpEvent_Function_Ack) {
		if (port >= VTTY_PORTS)
			return;

		spin_lock_irqsave(&consolelock, flags);
		/* Got the lock, don't cause console output */

		if (event->xRc == HvLpEvent_Rc_Good) {
			sndMsgSeq[port] = sndMsgAck[port] = 0;

			/* This line allows connections from the primary
			 * partition but once one is connected from the
			 * primary partition nothing short of a reboot
			 * of linux will allow access from the hosting
			 * partition again without a required iSeries fix.
			 */
			port_info[port].lp = event->xTargetLp;
		}

		spin_unlock_irqrestore(&consolelock, flags);
		if (event->xRc != HvLpEvent_Rc_Good)
			printk(KERN_WARNING_VIO
			       "viocons: event->xRc != HvLpEvent_Rc_Good, event->xRc == (%d).\n",
			       event->xRc);

		if (event->xCorrelationToken != 0) {
			unsigned long semptr = event->xCorrelationToken;
			up((struct semaphore *) semptr);
		} else
			printk(KERN_WARNING_VIO
			       "viocons: wierd...got open ack without semaphore\n");
	} else {
		/* This had better require an ack, otherwise complain
		 */
		if (event->xFlags.xAckInd != HvLpEvent_AckInd_DoAck) {
			printk(KERN_WARNING_VIO
			       "viocons: viocharopen without ack bit!\n");
			return;
		}

		spin_lock_irqsave(&consolelock, flags);
		/* Got the lock, don't cause console output */

		/* Make sure this is a good virtual tty */
		if (port >= VTTY_PORTS) {
			event->xRc = HvLpEvent_Rc_SubtypeError;
			cevent->mSubTypeRc = viorc_openRejected;
			/*
			 * Flag state here since we can't printk while holding
			 * a spinlock.
			 */
			reject = 1;
		} else if ((port_info[port].lp != HvLpIndexInvalid) &&
			   (port_info[port].lp != event->xSourceLp)) {
			/*
			 * If this is tty is already connected to a different
			 * partition, fail.
			 */
			event->xRc = HvLpEvent_Rc_SubtypeError;
			cevent->mSubTypeRc = viorc_openRejected;
			reject = 2;
		} else {
			port_info[port].lp = event->xSourceLp;
			event->xRc = HvLpEvent_Rc_Good;
			cevent->mSubTypeRc = viorc_good;
			sndMsgSeq[port] = sndMsgAck[port] = 0;
			reject = 0;
		}

		spin_unlock_irqrestore(&consolelock, flags);

		if (reject == 1)
			printk
			    (KERN_WARNING_VIO "viocons: console open rejected : bad virtual tty.\n");
		else if (reject == 2)
			printk
			    (KERN_WARNING_VIO "viocons: console open rejected : console in exclusive use by another partition.\n");
		/*
		 * Don't leave this here to clutter up the log unless it is
		 * needed for debug.
		 *
		 * else
		 *      printk(KERN_INFO_VIO "viocons: console open event Good!\n");
		 *
		 */

		/* Return the acknowledgement */
		HvCallEvent_ackLpEvent(event);
	}
}

/* Handle a close charLpEvent.  This should ONLY be an Interrupt because the
 * virtual console should never actually issue a close event to the hypervisor
 * because the virtual console never goes away.  A close event coming from the
 * hypervisor simply means that there are no client consoles connected to the
 * virtual console.
 *
 * Regardless of the number of connections masqueraded on the other side of
 * the hypervisor ONLY ONE close event should be called to accompany the ONE
 * open event that is called.  The close event should ONLY be called when NO
 * MORE connections (masqueraded or not) exist on the other side of the
 * hypervisor.
 */
static void vioHandleCloseEvent(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *) event;
	u8 port = cevent->virtualDevice;

	if (event->xFlags.xFunction == HvLpEvent_Function_Int) {
		if (port >= VTTY_PORTS) {
			printk(KERN_WARNING_VIO "viocons: close message from invalid virtual device.\n");
			return;
		}

		/* For closes, just mark the console partition invalid */
		spin_lock_irqsave(&consolelock, flags);
		/* Got the lock, don't cause console output */

		if (port_info[port].lp == event->xSourceLp)
			port_info[port].lp = HvLpIndexInvalid;

		spin_unlock_irqrestore(&consolelock, flags);
		printk(KERN_INFO_VIO
		       "console close from %d\n", event->xSourceLp);
	} else {
		printk(KERN_WARNING_VIO
		       "console got unexpected close acknowlegement\n");
	}
}

/* Handle a config charLpEvent.  Could be either interrupt or ack
 */
static void vioHandleConfig(struct HvLpEvent *event)
{
	struct viocharlpevent *cevent = (struct viocharlpevent *) event;
	int len;

	len = cevent->immediateDataLen;
	HvCall_writeLogBuffer(cevent->immediateData,
			      cevent->immediateDataLen);

	if (cevent->immediateData[0] == 0x01) {
		printk(KERN_INFO_VIO
		       "console window resized to %d: %d: %d: %d\n",
		       cevent->immediateData[1],
		       cevent->immediateData[2],
		       cevent->immediateData[3], cevent->immediateData[4]);
	} else {
		printk(KERN_WARNING_VIO "console unknown config event\n");
	}
	return;
}

/* Handle a data charLpEvent. */
static void vioHandleData(struct HvLpEvent *event)
{
	struct tty_struct *tty;
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *) event;
	struct port_info_tag *pi;
	int index;
	u8 port = cevent->virtualDevice;

	if (port >= VTTY_PORTS) {
		printk(KERN_WARNING_VIO
		       "console data on invalid virtual device %d\n",
		       port);
		return;
	}
	/*
	 * Change 05/01/2003 - Ryan Arnold: If a partition other than
	 * the current exclusive partition tries to send us data
	 * events then just drop them on the floor because we don't
	 * want his stinking data.  He isn't authorized to receive
	 * data because he wasn't the first one to get the console,
	 * therefore he shouldn't be allowed to send data either.
	 * This will work without an iSeries fix.
	 */
	if (port_info[port].lp != event->xSourceLp)
		return;

	/* Hold the spinlock so that we don't take an interrupt that
	 * changes tty between the time we fetch the port_info_tag
	 * pointer and the time we paranoia check.
	 */
	spin_lock_irqsave(&consolelock, flags);

	tty = port_info[port].tty;

	if (tty == NULL) {
		spin_unlock_irqrestore(&consolelock, flags);
		printk(KERN_WARNING_VIO
		       "no tty for virtual device %d\n", port);
		return;
	}

	if (tty->magic != TTY_MAGIC) {
		spin_unlock_irqrestore(&consolelock, flags);
		printk(KERN_WARNING_VIO "tty bad magic\n");
		return;
	}

	/*
	 * Just to be paranoid, make sure the tty points back to this port
	 */
	pi = (struct port_info_tag *) tty->driver_data;

	if (!pi || viotty_paranoia_check(pi, tty->device, "vioHandleData")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}
	spin_unlock_irqrestore(&consolelock, flags);

	/* Change 07/21/2003 - Ryan Arnold: functionality added to support sysrq
	 * utilizing ^O as the sysrq key.  The sysrq functionality will only work
	 * if built into the kernel and then only if sysrq is enabled through the
	 * proc filesystem.
	 */
	for ( index = 0; index < cevent->immediateDataLen; ++index ) {
#ifdef CONFIG_MAGIC_SYSRQ /* Handle the SysRq */
		if( sysrq_ctls.enabled ) {
			/* 0x0f is the ascii character for ^O */
			if(cevent->immediateData[index] == '\x0f') {
				vio_sysrq_pressed = 1;
				/* continue because we don't want to add the sysrq key into
				 * the data string.*/
				continue;
			} else if (vio_sysrq_pressed) {
				handle_sysrq(cevent->immediateData[index], NULL, NULL, tty);
				vio_sysrq_pressed = 0;
				/* continue because we don't want to add the sysrq
				 * sequence into the data string.*/
				continue;
			}
		}
#endif
		/* The sysrq sequence isn't included in this check if sysrq is enabled
		 * and compiled into the kernel because the sequence will never get
		 * inserted into the buffer.  Don't attempt to copy more data into the
		 * buffer than we have room for because it would fail without
		 * indication.
		 */
		if( tty->flip.count + 1 > TTY_FLIPBUF_SIZE ) {
			printk(KERN_WARNING_VIO "console input buffer overflow!\n");
			break;
		}
		else {
			tty_insert_flip_char(tty, cevent->immediateData[index], TTY_NORMAL);
		}
	}

	/* if cevent->immediateDataLen == 0 then no data was added to the buffer and flip.count == 0 */
	if (tty->flip.count) {
		/* The next call resets flip.count when the data is flushed. */
		tty_flip_buffer_push(tty);
	}
}

/* Handle an ack charLpEvent. */
static void vioHandleAck(struct HvLpEvent *event)
{
	struct viocharlpevent *cevent = (struct viocharlpevent *) event;
	unsigned long flags;
	u8 port = cevent->virtualDevice;

	if (port >= VTTY_PORTS) {
		printk(KERN_WARNING_VIO
		       "viocons: data on invalid virtual device.\n");
		return;
	}

	spin_lock_irqsave(&consolelock, flags);
	sndMsgAck[port] = event->xCorrelationToken;
	spin_unlock_irqrestore(&consolelock, flags);

	if (overflow[port].bufferUsed)
		sendBuffers(port, port_info[port].lp);
}

/* Handle charLpEvents and route to the appropriate routine
 */
static void vioHandleCharEvent(struct HvLpEvent *event)
{
	int charminor;

	if (event == NULL) {
		return;
	}
	charminor = event->xSubtype & VIOMINOR_SUBTYPE_MASK;
	switch (charminor) {
	case viocharopen:
		vioHandleOpenEvent(event);
		break;
	case viocharclose:
		vioHandleCloseEvent(event);
		break;
	case viochardata:
		vioHandleData(event);
		break;
	case viocharack:
		vioHandleAck(event);
		break;
	case viocharconfig:
		vioHandleConfig(event);
		break;
	default:
		if ((event->xFlags.xFunction == HvLpEvent_Function_Int) &&
		    (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

/* Send an open event
 */
static int viocons_sendOpen(HvLpIndex remoteLp, u8 port, void *sem)
{
	return HvCallEvent_signalLpEventFast(remoteLp,
					     HvLpEvent_Type_VirtualIo,
					     viomajorsubtype_chario
					     | viocharopen,
					     HvLpEvent_AckInd_DoAck,
					     HvLpEvent_AckType_ImmediateAck,
					     viopath_sourceinst
					     (remoteLp),
					     viopath_targetinst
					     (remoteLp),
					     (u64) (unsigned long)
					     sem, VIOVERSION << 16,
					     ((u64) port << 48), 0, 0, 0);

}

int __init viocons_init2(void)
{
	DECLARE_MUTEX_LOCKED(Semaphore);
	int rc;

	/*
	 * Now open to the primary LP
	 */
	printk(KERN_INFO_VIO "console open path to primary\n");
	rc = viopath_open(HvLpConfig_getPrimaryLpIndex(), viomajorsubtype_chario, viochar_window + 2);	/* +2 for fudge */
	if (rc) {
		printk(KERN_WARNING_VIO
		       "console error opening to primary %d\n", rc);
	}

	if (viopath_hostLp == HvLpIndexInvalid) {
		vio_set_hostlp();
	}

	/*
	 * And if the primary is not the same as the hosting LP, open to the 
	 * hosting lp
	 */
	if ((viopath_hostLp != HvLpIndexInvalid) &&
	    (viopath_hostLp != HvLpConfig_getPrimaryLpIndex())) {
		printk(KERN_INFO_VIO
		       "console open path to hosting (%d)\n",
		       viopath_hostLp);
		rc = viopath_open(viopath_hostLp, viomajorsubtype_chario, viochar_window + 2);	/* +2 for fudge */
		if (rc) {
			printk(KERN_WARNING_VIO
			       "console error opening to partition %d: %d\n",
			       viopath_hostLp, rc);
		}
	}

	if (vio_setHandler(viomajorsubtype_chario, vioHandleCharEvent) < 0) {
		printk(KERN_WARNING_VIO
		       "Error seting handler for console events!\n");
	}

	printk(KERN_INFO_VIO "console major number is %d\n", TTY_MAJOR);

	/* First, try to open the console to the hosting lp.
	 * Wait on a semaphore for the response.
	 */
	if ((viopath_isactive(viopath_hostLp)) &&
	    (viocons_sendOpen(viopath_hostLp, 0, &Semaphore) == 0)) {
		printk(KERN_INFO_VIO
		       "opening console to hosting partition %d\n",
		       viopath_hostLp);
		down(&Semaphore);
	}

	/*
	 * If we don't have an active console, try the primary
	 */
	if ((!viopath_isactive(port_info[0].lp)) &&
	    (viopath_isactive(HvLpConfig_getPrimaryLpIndex())) &&
	    (viocons_sendOpen
	     (HvLpConfig_getPrimaryLpIndex(), 0, &Semaphore) == 0)) {
		printk(KERN_INFO_VIO
		       "opening console to primary partition\n");
		down(&Semaphore);
	}

	/* Initialize the tty_driver structure */
	memset(&viotty_driver, 0, sizeof(struct tty_driver));
	viotty_driver.magic = TTY_DRIVER_MAGIC;
	viotty_driver.driver_name = "vioconsole";
#if defined(CONFIG_DEVFS_FS)
	viotty_driver.name = "tty%d";
#else
	viotty_driver.name = "tty";
#endif
	viotty_driver.major = TTY_MAJOR;
	viotty_driver.minor_start = 1;
	viotty_driver.name_base = 1;
	viotty_driver.num = VTTY_PORTS;
	viotty_driver.type = TTY_DRIVER_TYPE_CONSOLE;
	viotty_driver.subtype = 1;
	viotty_driver.init_termios = tty_std_termios;
	viotty_driver.flags =
	    TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
	viotty_driver.refcount = &viotty_refcount;
	viotty_driver.table = viotty_table;
	viotty_driver.termios = viotty_termios;
	viotty_driver.termios_locked = viotty_termios_locked;

	viotty_driver.open = viotty_open;
	viotty_driver.close = viotty_close;
	viotty_driver.write = viotty_write;
	viotty_driver.put_char = viotty_put_char;
	viotty_driver.flush_chars = NULL;
	viotty_driver.write_room = viotty_write_room;
	viotty_driver.chars_in_buffer = viotty_chars_in_buffer;
	viotty_driver.flush_buffer = NULL;
	viotty_driver.ioctl = NULL;
	viotty_driver.throttle = NULL;
	viotty_driver.unthrottle = NULL;
	viotty_driver.set_termios = NULL;
	viotty_driver.stop = NULL;
	viotty_driver.start = NULL;
	viotty_driver.hangup = NULL;
	viotty_driver.break_ctl = NULL;
	viotty_driver.send_xchar = NULL;
	viotty_driver.wait_until_sent = NULL;

	viottyS_driver = viotty_driver;
#if defined(CONFIG_DEVFS_FS)
	viottyS_driver.name = "ttyS%d";
#else
	viottyS_driver.name = "ttyS";
#endif
	viottyS_driver.major = TTY_MAJOR;
	viottyS_driver.minor_start = VIOTTY_SERIAL_START;
	viottyS_driver.type = TTY_DRIVER_TYPE_SERIAL;
	viottyS_driver.table = viottyS_table;
	viottyS_driver.termios = viottyS_termios;
	viottyS_driver.termios_locked = viottyS_termios_locked;

	if (tty_register_driver(&viotty_driver)) {
		printk(KERN_WARNING_VIO
		       "Couldn't register console driver\n");
	}

	if (tty_register_driver(&viottyS_driver)) {
		printk(KERN_WARNING_VIO
		       "Couldn't register console S driver\n");
	}
	/* Now create the vcs and vcsa devfs entries so mingetty works */
#if defined(CONFIG_DEVFS_FS)
	{
		struct tty_driver temp_driver = viotty_driver;
		int i;

		temp_driver.name = "vcs%d";
		for (i = 0; i < VTTY_PORTS; i++)
			tty_register_devfs(&temp_driver,
					   0, i + temp_driver.minor_start);

		temp_driver.name = "vcsa%d";
		for (i = 0; i < VTTY_PORTS; i++)
			tty_register_devfs(&temp_driver,
					   0, i + temp_driver.minor_start);

		/* For compatibility with some earlier code only!
		 * This will go away!!!
		 */
		temp_driver.name = "viocons/%d";
		temp_driver.name_base = 0;
		for (i = 0; i < VTTY_PORTS; i++)
			tty_register_devfs(&temp_driver,
					   0, i + temp_driver.minor_start);
	}
#endif

	/* Create the proc entry */
	iSeries_proc_callback(&viocons_proc_init);

	/* Fetch memory for the cfu buffer */
	viocons_init_cfu_buffer();

	return 0;
}

void __init viocons_init(void)
{
	int i;
	printk(KERN_INFO_VIO "registering console\n");

	memset(&port_info, 0x00, sizeof(port_info));
	for (i = 0; i < VTTY_PORTS; i++) {
		sndMsgSeq[i] = sndMsgAck[i] = 0;
		port_info[i].port = i;
		port_info[i].lp = HvLpIndexInvalid;
		port_info[i].magic = VIOTTY_MAGIC;
	}

	register_console(&viocons);
	memset(overflow, 0x00, sizeof(overflow));
	debug = 0;

	HvCall_setLogBufferFormatAndCodepage(HvCall_LogBuffer_ASCII, 437);
}
