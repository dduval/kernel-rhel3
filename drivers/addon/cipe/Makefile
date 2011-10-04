#   -*-Makefile-*- template for the CIPE kernel module and driver.
#
#   Copyright 1996 Olaf Titz <olaf@bigred.inka.de>
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License
#   as published by the Free Software Foundation; either version
#   2 of the License, or (at your option) any later version.

O_TARGET := cipcb.o

obj-y := module.o device.o sock.o output.o encaps.o crc32.o bf.o
obj-m := $(O_TARGET)

ifeq ($(ARCH),i386)
obj-y += bf-i386.o
endif

include $(TOPDIR)/Rules.make

ifeq ($(ARCH),i386)
bf-i386.o: bf-i386.S
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<
endif
