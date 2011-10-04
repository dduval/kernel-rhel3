#
# Makefile for the LPFCDFC driver
#
#

O_TARGET := lpfcdfc.o
EXTRA_CFLAGS += -I$(TOPDIR)/drivers/scsi -I../
obj-m:= $(O_TARGET)

obj-y:= lpfc_cdev.o		\
	lpfc_util_ioctl.o	\
	lpfc_hbaapi_ioctl.o	\
	lpfc_debug_ioctl.o	\
	lpfc_logmsg.o

lpfc_logmsg.c:
	ln -sf ../lpfc_logmsg.c lpfc_logmsg.c

include $(TOPDIR)/Rules.make
