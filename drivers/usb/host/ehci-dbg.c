/*
 * Copyright (c) 2001-2002 by David Brownell
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* this file is part of ehci-hcd.c */

#ifdef DEBUG
#define ehci_dbg(ehci, fmt, args...) \
	printk(KERN_DEBUG "%s %s: " fmt , hcd_name , \
		(ehci)->hcd.pdev->slot_name , ## args )
#else
#define ehci_dbg(ehci, fmt, args...) do { } while (0)
#endif

#define ehci_err(ehci, fmt, args...) \
	printk(KERN_ERR "%s %s: " fmt , hcd_name , \
		(ehci)->hcd.pdev->slot_name , ## args )
#define ehci_info(ehci, fmt, args...) \
	printk(KERN_INFO "%s %s: " fmt , hcd_name , \
		(ehci)->hcd.pdev->slot_name , ## args )
#define ehci_warn(ehci, fmt, args...) \
	printk(KERN_WARNING "%s %s: " fmt , hcd_name , \
		(ehci)->hcd.pdev->slot_name , ## args )


#ifdef EHCI_VERBOSE_DEBUG
#	define vdbg dbg
#	define ehci_vdbg ehci_dbg
#else
#	define vdbg(fmt,args...) do { } while (0)
#	define ehci_vdbg(ehci, fmt, args...) do { } while (0)
#endif

#ifdef	DEBUG

/* check the values in the HCSPARAMS register
 * (host controller _Structural_ parameters)
 * see EHCI spec, Table 2-4 for each value
 */
static void dbg_hcs_params (struct ehci_hcd *ehci, char *label)
{
	u32	params = readl (&ehci->caps->hcs_params);

	ehci_dbg (ehci,
		"%s hcs_params 0x%x dbg=%d%s cc=%d pcc=%d%s%s ports=%d\n",
		label, params,
		HCS_DEBUG_PORT (params),
		HCS_INDICATOR (params) ? " ind" : "",
		HCS_N_CC (params),
		HCS_N_PCC (params),
	        HCS_PORTROUTED (params) ? "" : " ordered",
		HCS_PPC (params) ? "" : " !ppc",
		HCS_N_PORTS (params)
		);
	/* Port routing, per EHCI 0.95 Spec, Section 2.2.5 */
	if (HCS_PORTROUTED (params)) {
		int i;
		char buf [46], tmp [7], byte;

		buf[0] = 0;
		for (i = 0; i < HCS_N_PORTS (params); i++) {
			byte = readb (&ehci->caps->portroute[(i>>1)]);
			sprintf(tmp, "%d ", 
				((i & 0x1) ? ((byte)&0xf) : ((byte>>4)&0xf)));
			strcat(buf, tmp);
		}
		ehci_dbg (ehci, "%s portroute %s\n",
				label, buf);
	}
}
#else

static inline void dbg_hcs_params (struct ehci_hcd *ehci, char *label) {}

#endif

#ifdef	DEBUG

/* check the values in the HCCPARAMS register
 * (host controller _Capability_ parameters)
 * see EHCI Spec, Table 2-5 for each value
 * */
static void dbg_hcc_params (struct ehci_hcd *ehci, char *label)
{
	u32	params = readl (&ehci->caps->hcc_params);

	if (HCC_ISOC_CACHE (params)) {
		ehci_dbg (ehci,
		     "%s hcc_params %04x caching frame %s%s%s\n",
		     label, params,
		     HCC_PGM_FRAMELISTLEN (params) ? "256/512/1024" : "1024",
		     HCC_CANPARK (params) ? " park" : "",
		     HCC_64BIT_ADDR (params) ? " 64 bit addr" : "");
	} else {
		ehci_dbg (ehci,
		     "%s hcc_params %04x thresh %d uframes %s%s%s\n",
		     label,
		     params,
		     HCC_ISOC_THRES (params),
		     HCC_PGM_FRAMELISTLEN (params) ? "256/512/1024" : "1024",
		     HCC_CANPARK (params) ? " park" : "",
		     HCC_64BIT_ADDR (params) ? " 64 bit addr" : "");
	}
}
#else

static inline void dbg_hcc_params (struct ehci_hcd *ehci, char *label) {}

#endif

#ifdef	DEBUG

static void __attribute__((__unused__))
dbg_qh (char *label, struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	dbg ("%s %p n%08x info1 %x info2 %x hw_curr %x qtd_next %x", label,
		qh, qh->hw_next, qh->hw_info1, qh->hw_info2,
		qh->hw_current, qh->hw_qtd_next);
	dbg ("  alt+nak+t= %x, token= %x, page0= %x, page1= %x",
		qh->hw_alt_next, qh->hw_token,
		qh->hw_buf [0], qh->hw_buf [1]);
	if (qh->hw_buf [2]) {
		dbg ("  page2= %x, page3= %x, page4= %x",
			qh->hw_buf [2], qh->hw_buf [3],
			qh->hw_buf [4]);
	}
}

static int __attribute__((__unused__))
dbg_status_buf (char *buf, unsigned len, char *label, u32 status)
{
	return snprintf (buf, len,
		"%s%sstatus %04x%s%s%s%s%s%s%s%s%s%s",
		label, label [0] ? " " : "", status,
		(status & STS_ASS) ? " Async" : "",
		(status & STS_PSS) ? " Periodic" : "",
		(status & STS_RECL) ? " Recl" : "",
		(status & STS_HALT) ? " Halt" : "",
		(status & STS_IAA) ? " IAA" : "",
		(status & STS_FATAL) ? " FATAL" : "",
		(status & STS_FLR) ? " FLR" : "",
		(status & STS_PCD) ? " PCD" : "",
		(status & STS_ERR) ? " ERR" : "",
		(status & STS_INT) ? " INT" : ""
		);
}

static int __attribute__((__unused__))
dbg_intr_buf (char *buf, unsigned len, char *label, u32 enable)
{
	return snprintf (buf, len,
		"%s%sintrenable %02x%s%s%s%s%s%s",
		label, label [0] ? " " : "", enable,
		(enable & STS_IAA) ? " IAA" : "",
		(enable & STS_FATAL) ? " FATAL" : "",
		(enable & STS_FLR) ? " FLR" : "",
		(enable & STS_PCD) ? " PCD" : "",
		(enable & STS_ERR) ? " ERR" : "",
		(enable & STS_INT) ? " INT" : ""
		);
}

static const char *const fls_strings [] =
    { "1024", "512", "256", "??" };

static int dbg_command_buf (char *buf, unsigned len, char *label, u32 command)
{
	return snprintf (buf, len,
		"%s%scommand %06x %s=%d ithresh=%d%s%s%s%s period=%s%s %s",
		label, label [0] ? " " : "", command,
		(command & CMD_PARK) ? "park" : "(park)",
		CMD_PARK_CNT (command),
		(command >> 16) & 0x3f,
		(command & CMD_LRESET) ? " LReset" : "",
		(command & CMD_IAAD) ? " IAAD" : "",
		(command & CMD_ASE) ? " Async" : "",
		(command & CMD_PSE) ? " Periodic" : "",
		fls_strings [(command >> 2) & 0x3],
		(command & CMD_RESET) ? " Reset" : "",
		(command & CMD_RUN) ? "RUN" : "HALT"
		);
}

static int
dbg_port_buf (char *buf, unsigned len, char *label, int port, u32 status)
{
	char	*sig;

	/* signaling state */
	switch (status & (3 << 10)) {
	case 0 << 10: sig = "se0"; break;
	case 1 << 10: sig = "k"; break;		/* low speed */
	case 2 << 10: sig = "j"; break;
	default: sig = "?"; break;
	}

	return snprintf (buf, len,
		"%s%sport %d status %06x%s%s sig=%s %s%s%s%s%s%s%s%s%s",
		label, label [0] ? " " : "", port, status,
		(status & PORT_POWER) ? " POWER" : "",
		(status & PORT_OWNER) ? " OWNER" : "",
		sig,
		(status & PORT_RESET) ? " RESET" : "",
		(status & PORT_SUSPEND) ? " SUSPEND" : "",
		(status & PORT_RESUME) ? " RESUME" : "",
		(status & PORT_OCC) ? " OCC" : "",
		(status & PORT_OC) ? " OC" : "",
		(status & PORT_PEC) ? " PEC" : "",
		(status & PORT_PE) ? " PE" : "",
		(status & PORT_CSC) ? " CSC" : "",
		(status & PORT_CONNECT) ? " CONNECT" : ""
	    );
}

#else
static inline void __attribute__((__unused__))
dbg_qh (char *label, struct ehci_hcd *ehci, struct ehci_qh *qh)
{}

static inline int __attribute__((__unused__))
dbg_status_buf (char *buf, unsigned len, char *label, u32 status)
{ return 0; }

static inline int __attribute__((__unused__))
dbg_command_buf (char *buf, unsigned len, char *label, u32 command)
{ return 0; }

static inline int __attribute__((__unused__))
dbg_intr_buf (char *buf, unsigned len, char *label, u32 enable)
{ return 0; }

static inline int __attribute__((__unused__))
dbg_port_buf (char *buf, unsigned len, char *label, int port, u32 status)
{ return 0; }

#endif	/* DEBUG */

/* functions have the "wrong" filename when they're output... */
#define dbg_status(ehci, label, status) { \
	char _buf [80]; \
	dbg_status_buf (_buf, sizeof _buf, label, status); \
	ehci_dbg (ehci, "%s\n", _buf); \
}

#define dbg_cmd(ehci, label, command) { \
	char _buf [80]; \
	dbg_command_buf (_buf, sizeof _buf, label, command); \
	ehci_dbg (ehci, "%s\n", _buf); \
}

#define dbg_port(ehci, label, port, status) { \
	char _buf [80]; \
	dbg_port_buf (_buf, sizeof _buf, label, port, status); \
	ehci_dbg (ehci, "%s\n", _buf); \
}

/*-------------------------------------------------------------------------*/

#ifdef STUB_DEBUG_FILES

static inline void create_debug_files (struct ehci_hcd *bus) { }
static inline void remove_debug_files (struct ehci_hcd *bus) { }

#else

/* troubleshooting help: expose state in driverfs */

#define speed_char(info1) ({ char tmp; \
		switch (info1 & (3 << 12)) { \
		case 0 << 12: tmp = 'f'; break; \
		case 1 << 12: tmp = 'l'; break; \
		case 2 << 12: tmp = 'h'; break; \
		default: tmp = '?'; break; \
		}; tmp; })

static inline char token_mark (u32 token)
{
	token = le32_to_cpu (token);
	if (token & QTD_STS_ACTIVE)
		return '*';
	if (token & QTD_STS_HALT)
		return '-';
	if (QTD_PID (token) != 1 /* not IN: OUT or SETUP */
			|| QTD_LENGTH (token) == 0)
		return ' ';
	/* tries to advance through hw_alt_next */
	return '/';
}

static void qh_lines (
	struct ehci_hcd *ehci,
	struct ehci_qh *qh,
	char **nextp,
	unsigned *sizep
)
{
	u32			scratch;
	u32			hw_curr;
	struct list_head	*entry;
	struct ehci_qtd		*td;
	unsigned		temp;
	unsigned		size = *sizep;
	char			*next = *nextp;
	char			mark;

	mark = token_mark (qh->hw_token);
	if (mark == '/') {	/* qh_alt_next controls qh advance? */
		if ((qh->hw_alt_next & QTD_MASK) == ehci->async->hw_alt_next)
			mark = '#';	/* blocked */
		else if (qh->hw_alt_next & cpu_to_le32 (0x01))
			mark = '.';	/* use hw_qtd_next */
		/* else alt_next points to some other qtd */
	}
	scratch = cpu_to_le32p (&qh->hw_info1);
	hw_curr = (mark == '*') ? cpu_to_le32p (&qh->hw_current) : 0;
	temp = snprintf (next, size,
			"qh/%p dev%d %cs ep%d %08x %08x (%08x%c %s nak%d)",
			qh, scratch & 0x007f,
			speed_char (scratch),
			(scratch >> 8) & 0x000f,
			scratch, cpu_to_le32p (&qh->hw_info2),
			cpu_to_le32p (&qh->hw_token), mark,
			(cpu_to_le32 (0x8000000) & qh->hw_token)
				? "data0" : "data1",
			(cpu_to_le32p (&qh->hw_alt_next) >> 1) & 0x0f);
	size -= temp;
	next += temp;

	/* hc may be modifying the list as we read it ... */
	list_for_each (entry, &qh->qtd_list) {
		td = list_entry (entry, struct ehci_qtd, qtd_list);
		scratch = cpu_to_le32p (&td->hw_token);
		mark = ' ';
		if (hw_curr == td->qtd_dma)
			mark = '*';
		else if (qh->hw_qtd_next == td->qtd_dma)
			mark = '+';
		else if (QTD_LENGTH (scratch)) {
			if (td->hw_alt_next == ehci->async->hw_alt_next)
				mark = '#';
			else if (td->hw_alt_next != EHCI_LIST_END)
				mark = '/';
		}
		temp = snprintf (next, size,
				"\n\t%p%c%s len=%d %08x urb %p",
				td, mark, ({ char *tmp;
				 switch ((scratch>>8)&0x03) {
				 case 0: tmp = "out"; break;
				 case 1: tmp = "in"; break;
				 case 2: tmp = "setup"; break;
				 default: tmp = "?"; break;
				 } tmp;}),
				(scratch >> 16) & 0x7fff,
				scratch,
				td->urb);
		if (temp < 0)
			temp = 0;
		else if (size < temp)
			temp = size;
		size -= temp;
		next += temp;
		if (temp == size)
			goto done;
	}

	temp = snprintf (next, size, "\n");
	if (temp < 0)
		temp = 0;
	else if (size < temp)
		temp = size;
	size -= temp;
	next += temp;

done:
	*sizep = size;
	*nextp = next;
}

static ssize_t
show_async (struct device *dev, char *buf)
{
	struct pci_dev		*pdev;
	struct ehci_hcd		*ehci;
	unsigned long		flags;
	unsigned		temp, size;
	char			*next;
	struct ehci_qh		*qh;

	pdev = container_of (dev, struct pci_dev, dev);
	ehci = container_of (pci_get_drvdata (pdev), struct ehci_hcd, hcd);
	next = buf;
	size = PAGE_SIZE;

	/* dumps a snapshot of the async schedule.
	 * usually empty except for long-term bulk reads, or head.
	 * one QH per line, and TDs we know about
	 */
	spin_lock_irqsave (&ehci->lock, flags);
	for (qh = ehci->async->qh_next.qh; size > 0 && qh; qh = qh->qh_next.qh)
		qh_lines (ehci, qh, &next, &size);
	if (ehci->reclaim && size > 0) {
		temp = snprintf (next, size, "\nreclaim =\n");
		size -= temp;
		next += temp;

		for (qh = ehci->reclaim; size > 0 && qh; qh = qh->reclaim)
			qh_lines (ehci, qh, &next, &size);
	}
	spin_unlock_irqrestore (&ehci->lock, flags);

	return PAGE_SIZE - size;
}
static DEVICE_ATTR (async, S_IRUGO, show_async, NULL);

#define DBG_SCHED_LIMIT 64

static ssize_t
show_periodic (struct device *dev, char *buf)
{
	struct pci_dev		*pdev;
	struct ehci_hcd		*ehci;
	unsigned long		flags;
	union ehci_shadow	p, *seen;
	unsigned		temp, size, seen_count;
	char			*next;
	unsigned		i, tag;

	if (!(seen = kmalloc (DBG_SCHED_LIMIT * sizeof *seen, SLAB_ATOMIC)))
		return 0;
	seen_count = 0;

	pdev = container_of (dev, struct pci_dev, dev);
	ehci = container_of (pci_get_drvdata (pdev), struct ehci_hcd, hcd);
	next = buf;
	size = PAGE_SIZE;

	temp = snprintf (next, size, "size = %d\n", ehci->periodic_size);
	size -= temp;
	next += temp;

	/* dump a snapshot of the periodic schedule.
	 * iso changes, interrupt usually doesn't.
	 */
	spin_lock_irqsave (&ehci->lock, flags);
	for (i = 0; i < ehci->periodic_size; i++) {
		p = ehci->pshadow [i];
		if (!p.ptr)
			continue;
		tag = Q_NEXT_TYPE (ehci->periodic [i]);

		temp = snprintf (next, size, "%4d: ", i);
		size -= temp;
		next += temp;

		do {
			switch (tag) {
			case Q_TYPE_QH:
				temp = snprintf (next, size, " qh%d/%p",
						p.qh->period, p.qh);
				size -= temp;
				next += temp;
				for (temp = 0; temp < seen_count; temp++) {
					if (seen [temp].ptr == p.ptr)
						break;
				}
				/* show more info the first time around */
				if (temp == seen_count) {
					u32	scratch = cpu_to_le32p (
							&p.qh->hw_info1);

					temp = snprintf (next, size,
						" (%cs dev%d ep%d [%d/%d] %d)",
						speed_char (scratch),
						scratch & 0x007f,
						(scratch >> 8) & 0x000f,
						p.qh->usecs, p.qh->c_usecs,
						0x7ff & (scratch >> 16));

					/* FIXME TD info too */

					if (seen_count < DBG_SCHED_LIMIT)
						seen [seen_count++].qh = p.qh;
				} else
					temp = 0;
				tag = Q_NEXT_TYPE (p.qh->hw_next);
				p = p.qh->qh_next;
				break;
			case Q_TYPE_FSTN:
				temp = snprintf (next, size,
					" fstn-%8x/%p", p.fstn->hw_prev,
					p.fstn);
				tag = Q_NEXT_TYPE (p.fstn->hw_next);
				p = p.fstn->fstn_next;
				break;
			case Q_TYPE_ITD:
				temp = snprintf (next, size,
					" itd/%p", p.itd);
				tag = Q_NEXT_TYPE (p.itd->hw_next);
				p = p.itd->itd_next;
				break;
			case Q_TYPE_SITD:
				temp = snprintf (next, size,
					" sitd/%p", p.sitd);
				tag = Q_NEXT_TYPE (p.sitd->hw_next);
				p = p.sitd->sitd_next;
				break;
			}
			size -= temp;
			next += temp;
		} while (p.ptr);

		temp = snprintf (next, size, "\n");
		size -= temp;
		next += temp;
	}
	spin_unlock_irqrestore (&ehci->lock, flags);
	kfree (seen);

	return PAGE_SIZE - size;
}
static DEVICE_ATTR (periodic, S_IRUGO, show_periodic, NULL);

#undef DBG_SCHED_LIMIT

static ssize_t
show_registers (struct device *dev, char *buf)
{
	struct pci_dev		*pdev;
	struct ehci_hcd		*ehci;
	unsigned long		flags;
	unsigned		temp, size, i;
	char			*next, scratch [80];
	static char		fmt [] = "%*s\n";
	static char		label [] = "";

	pdev = container_of (dev, struct pci_dev, dev);
	ehci = container_of (pci_get_drvdata (pdev), struct ehci_hcd, hcd);

	next = buf;
	size = PAGE_SIZE;

	spin_lock_irqsave (&ehci->lock, flags);

	/* Capability Registers */
	i = HC_VERSION(readl (&ehci->caps->hc_capbase));
	temp = snprintf (next, size,
		"EHCI %x.%02x, hcd state %d (version " DRIVER_VERSION ")\n",
		i >> 8, i & 0x0ff, ehci->hcd.state);
	size -= temp;
	next += temp;

	// FIXME interpret both types of params
	i = readl (&ehci->caps->hcs_params);
	temp = snprintf (next, size, "structural params 0x%08x\n", i);
	size -= temp;
	next += temp;

	i = readl (&ehci->caps->hcc_params);
	temp = snprintf (next, size, "capability params 0x%08x\n", i);
	size -= temp;
	next += temp;

	/* Operational Registers */
	temp = dbg_status_buf (scratch, sizeof scratch, label,
			readl (&ehci->regs->status));
	temp = snprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = dbg_command_buf (scratch, sizeof scratch, label,
			readl (&ehci->regs->command));
	temp = snprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = dbg_intr_buf (scratch, sizeof scratch, label,
			readl (&ehci->regs->intr_enable));
	temp = snprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = snprintf (next, size, "uframe %04x\n",
			readl (&ehci->regs->frame_index));
	size -= temp;
	next += temp;

	for (i = 0; i < HCS_N_PORTS (ehci->hcs_params); i++) {
		temp = dbg_port_buf (scratch, sizeof scratch, label, i,
				readl (&ehci->regs->port_status [i]));
		temp = snprintf (next, size, fmt, temp, scratch);
		size -= temp;
		next += temp;
	}

	if (ehci->reclaim) {
		temp = snprintf (next, size, "reclaim qh %p%s\n",
				ehci->reclaim,
				ehci->reclaim_ready ? " ready" : "");
		size -= temp;
		next += temp;
	}

#ifdef EHCI_STATS
	temp = snprintf (next, size,
		"irq normal %ld err %ld reclaim %ld (lost %ld)\n",
		ehci->stats.normal, ehci->stats.error, ehci->stats.reclaim,
		ehci->stats.lost_iaa);
	size -= temp;
	next += temp;

	temp = snprintf (next, size, "complete %ld unlink %ld\n",
		ehci->stats.complete, ehci->stats.unlink);
	size -= temp;
	next += temp;
#endif

	spin_unlock_irqrestore (&ehci->lock, flags);

	return PAGE_SIZE - size;
}
static DEVICE_ATTR (registers, S_IRUGO, show_registers, NULL);

static inline void create_debug_files (struct ehci_hcd *bus)
{
	device_create_file (&bus->hcd.pdev->dev, &dev_attr_async);
	device_create_file (&bus->hcd.pdev->dev, &dev_attr_periodic);
	device_create_file (&bus->hcd.pdev->dev, &dev_attr_registers);
}

static inline void remove_debug_files (struct ehci_hcd *bus)
{
	device_remove_file (&bus->hcd.pdev->dev, &dev_attr_async);
	device_remove_file (&bus->hcd.pdev->dev, &dev_attr_periodic);
	device_remove_file (&bus->hcd.pdev->dev, &dev_attr_registers);
}

#endif /* STUB_DEBUG_FILES */

