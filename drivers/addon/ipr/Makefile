# File: drivers/addon/ipr/Makefile
# Makefile for the IBM Power RAID SCSI driver.

include version.mk

list-multi := ipr.o
ifeq ($(CONFIG_PPC_PSERIES),y)
  ipr-objs := iprdd.o lib/iprlib.o arch/ipr_pseries.o
else
ifeq ($(CONFIG_PPC_ISERIES),y)
  ipr-objs := iprdd.o lib/iprlib.o arch/ipr_iseries.o
else
  ipr-objs := iprdd.o lib/iprlib.o arch/ipr_generic.o
endif
endif

obj-$(CONFIG_SCSI_IPR) := ipr.o

EXTRA_CFLAGS += -I. -I../../scsi -I./arch -I./lib -DIPR_IPRDD $(IPR_DEFINES)

ipr.o: $(ipr-objs)
	$(LD) -r -o $@ $(ipr-objs)

include $(TOPDIR)/Rules.make

clean:
	rm -f *.o
