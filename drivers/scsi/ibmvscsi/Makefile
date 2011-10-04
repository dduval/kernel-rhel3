# Makefile for the IBM Virtual SCSI driver.

list-multi := ibmvscsic.o
ifeq ($(CONFIG_PPC_ISERIES),y)
  ibmvscsi-objs := ibmvscsi.o iseries_vscsi.o
else
  ibmvscsi-objs := ibmvscsi.o rpa_vscsi.o
endif

EXTRA_CFLAGS	+= -I$(TOPDIR)/drivers/iseries

obj-$(CONFIG_SCSI_IBMVSCSI) := ibmvscsic.o

ibmvscsic.o: $(ibmvscsi-objs)
	$(LD) -r -o $@ $(ibmvscsi-objs)

include $(TOPDIR)/Rules.make

clean:
	rm -f *.o
