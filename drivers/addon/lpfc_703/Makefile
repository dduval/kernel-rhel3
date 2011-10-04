#
# Makefile for the LPFC driver
#
#

O_TARGET := lpfc_703.o
EXTRA_CFLAGS := -I../../scsi
obj-$(CONFIG_SCSI_LPFC)	+= lpfc_703.o

LPFCOBJS= \
	lpfc_clock.o		\
	lpfc_ct.o		\
	lpfc_els.o		\
	lpfc_fcp.o		\
	lpfc_hbadisc.o		\
	lpfc_init.o		\
	lpfc_logmsg.o		\
	lpfc_mbox.o		\
	lpfc_mem.o		\
	lpfc_nportdisc.o	\
	lpfc_sched.o		\
	lpfc_scsiport.o		\
	lpfc_sli.o

include $(TOPDIR)/Rules.make

lpfc_703.o: $(LPFCOBJS)
	$(LD) -o $@ -r $(LPFCOBJS)

clean:
	rm -f *.o
