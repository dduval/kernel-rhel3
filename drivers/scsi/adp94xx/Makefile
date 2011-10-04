#
# Makefile for ADP94XX Linux device driver.
#

EXTRA_CFLAGS	+= -I$(TOPDIR)/include -I$(TOPDIR)/drivers/scsi -I. -DLINUX -D_linux -DSEQUENCER_UPDATE -DSATA_SKIP_FIX -DCONCURRENT_SUPPORT -DASD_RED_HAT_LINUX_KERNEL

ASD_DEBUG_CFLAGS := -UASD_DEBUG -Wunused -UASD_TEST -UASD_EH_SIMULATION 

EXTRA_CFLAGS += $(ASD_DEBUG_CFLAGS)

O_TARGET	:= adp94xx.o

obj-$(CONFIG_SCSI_ADP94XX) := $(O_TARGET)
list-multi	:= $(O_TARGET)

adp94xx-osm-objs := adp94xx_osm.o

adp94xx-hwi-objs := adp94xx_hwi.o

adp94xx-seq-objs := adp94xx_seq.o

adp94xx-sata-objs := adp94xx_sata.o

adp94xx-ioctl-objs := adp94xx_ioctl.o

adp94xx-discover-objs := adp94xx_discover.o

adp94xx-objs	:= $(adp94xx-osm-objs) $(adp94xx-hwi-objs) $(adp94xx-seq-objs) $(adp94xx-sata-objs) $(adp94xx-discover-objs) $(adp94xx-ioctl-objs)

MOD_DESTDIR = $(shell cd .. && $(CONFIG_SHELL) $(TOPDIR)/scripts/pathdown.sh)

include $(TOPDIR)/Rules.make

adp94xx.o: $(adp94xx-objs)
	$(LD) -r -o $@ $(adp94xx-objs)

clean:
	rm -f $(adp94xx-objs) $(O_TARGET)
