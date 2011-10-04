#
# Makefile for the old ServRAID ips driver
#

EXTRA_CFLAGS := -I$(TOPDIR)/drivers/scsi

obj-$(CONFIG_SCSI_IPS_60026) += ips_60026.o

include $(TOPDIR)/Rules.make

ips_60026.o : ips.c ips.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c ips.c -o $@
