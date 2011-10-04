# Copyright (C) 2001 By Joachim Martillo, Telford Tools, Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.

# File: drivers/net/WAN/atiXX50/Makefile
#
# Makefile for the Aurora ESSC based cards
# Specifically the 2520, 4020, 4520, 8520
#

all: SILX.o

O_TARGET := SILX.o

obj-y := cd1865.o
obj-m := $(O_TARGET)

EXTRA_CFLAGS += -I.

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s *~

