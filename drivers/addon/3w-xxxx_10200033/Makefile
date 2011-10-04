#
# Makefile for the old 3ware driver
#
EXTRA_CFLAGS := -I$(TOPDIR)/drivers/scsi

obj-$(CONFIG_BLK_DEV_3W_XXXX_RAID) += 3w-xxxx_10200033.o

include $(TOPDIR)/Rules.make

3w-xxxx_10200033.o: 3w-xxxx.c 3w-xxxx.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c 3w-xxxx.c -o $@

