#
# Makefile for the old ServRAID ips driver
#

EXTRA_CFLAGS := -I$(TOPDIR)/drivers/scsi

obj-$(CONFIG_SCSI_IPS) += ips_61107.o

include $(TOPDIR)/Rules.make

ips_61107.o : ips.c ips.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c ips.c -o $@
