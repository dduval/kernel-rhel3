/*
 *  Implemented in drivers/s390/char/ctrlchar.c
 *  Unified handling of special chars.
 *
 *    Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Fritz Elfert <felfert@millenux.com> <elfert@de.ibm.com>
 *
 */

struct tty_struct;

extern unsigned int ctrlchar_handle(const unsigned char *buf, int len,
    struct tty_struct *tty, int is_console);
extern void ctrlchar_init(void);

#define CTRLCHAR_CTRL  (0 << 8)
#define CTRLCHAR_NONE  (1 << 8)
#define CTRLCHAR_SYSRQ (2 << 8)

#define CTRLCHAR_MASK (~0xffu)
