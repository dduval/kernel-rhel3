#
# Makefile for the old cmpci sound driver
#

EXTRA_CFLAGS := -I$(TOPDIR)/drivers/sound

obj-$(CONFIG_SOUND_CMPCI) += cmpci_564.o

include $(TOPDIR)/Rules.make

cmpci_564.o : cmpci.c 
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c cmpci.c -o $@
