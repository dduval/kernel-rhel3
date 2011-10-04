#
# Makefile for older rev. Megaraid2 driver
#

EXTRA_CFLAGS := -I$(TOPDIR)/drivers/scsi

obj-$(CONFIG_SCSI_MEGARAID2)	+= megaraid_2101.o

include $(TOPDIR)/Rules.make

megaraid_2101.o : megaraid2.c megaraid2.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c megaraid2.c -o $@

