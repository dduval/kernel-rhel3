#
# Makefile for the LPFC driver
#
#

O_TARGET := lpfc.o
EXTRA_CFLAGS += -I$(TOPDIR)/drivers/scsi -DVARYIO=3 -DUSE_HIGHMEM_IO=3
obj-m:= $(O_TARGET)

obj-y:= lpfc_clock.o 		\
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

subdir-$(CONFIG_SCSI_LPFC) += lpfcdfc

include $(TOPDIR)/Rules.make
