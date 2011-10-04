#
# Makefile for the old ServRAID ips driver
#

EXTRA_CFLAGS := -I$(TOPDIR)/drivers/scsi

obj-$(CONFIG_SCSI_IPS) += ips_61052.o

include $(TOPDIR)/Rules.make

ips_61052.o : ips.c ips.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c ips.c -o $@
