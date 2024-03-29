/*
 *  drivers/s390/char/ctrlchar.c
 *  Unified handling of special chars.
 *
 *    Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Fritz Elfert <felfert@millenux.com> <elfert@de.ibm.com>
 *
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/sysrq.h>
#include <linux/ctype.h>
#include <asm/spinlock.h> /* P3: interrupt.h has extern spinlock_t foo */
#include <linux/interrupt.h>
#include <linux/tqueue.h>
#include <linux/tty.h>

#include <asm/ctrlchar.h>

#ifdef CONFIG_MAGIC_SYSRQ
static int ctrlchar_sysrq_key;
static struct tq_struct ctrlchar_tq;

static void
ctrlchar_handle_sysrq(void *tty)
{
	handle_sysrq(ctrlchar_sysrq_key, NULL, NULL, (struct tty_struct*) tty);
}
#endif

void
ctrlchar_init(void)
{
#ifdef CONFIG_MAGIC_SYSRQ
	static int init_done = 0;
 
	if (init_done++)
		return;
	INIT_LIST_HEAD(&ctrlchar_tq.list);
	ctrlchar_tq.sync = 0;
	ctrlchar_tq.routine = (void (*)(void *)) ctrlchar_handle_sysrq;
#endif
}

/**
 * Check for special chars at start of input.
 *
 * @param buf Console input buffer.
 * @param len Length of valid data in buffer.
 * @param tty The tty struct for this console.
 * @return CTRLCHAR_NONE, if nothing matched,
 *         CTRLCHAR_SYSRQ, if sysrq was encountered
 *         otherwise char to be inserted logically or'ed
 *         with CTRLCHAR_CTRL
 */
unsigned int
ctrlchar_handle(const unsigned char *buf, int len, struct tty_struct *tty,
    int is_console)
{
	if ((len < 2) || (len > 3))
		return CTRLCHAR_NONE;

	/* hat is 0xb1 in codepage 037 (US etc.) and thus */
	/* converted to 0x5e in ascii ('^') */
	if ((buf[0] != '^') && (buf[0] != '\252'))
		return CTRLCHAR_NONE;

#ifdef CONFIG_MAGIC_SYSRQ
	/* racy */
	if (is_console && len == 3 && buf[1] == '-') {
		ctrlchar_sysrq_key = buf[2];
		ctrlchar_tq.data = tty;
		queue_task(&ctrlchar_tq, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
		return CTRLCHAR_SYSRQ;
	}
#endif

	if (len != 2)
		return CTRLCHAR_NONE;

	switch (tolower(buf[1])) {
	case 'c':
		return INTR_CHAR(tty) | CTRLCHAR_CTRL;
	case 'd':
		return EOF_CHAR(tty)  | CTRLCHAR_CTRL;
	case 'z':
		return SUSP_CHAR(tty) | CTRLCHAR_CTRL;
	}
	return CTRLCHAR_NONE;
}
