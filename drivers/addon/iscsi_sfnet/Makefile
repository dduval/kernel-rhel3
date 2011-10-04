EXTRA_CFLAGS	+= -I$(TOPDIR)/drivers/scsi

O_TARGET	:= iscsi_sfnet.o

obj-m		:= $(O_TARGET)

obj-y		:= iscsi.o \
		   iscsi-probe.o \
		   iscsi-login.o \
		   iscsiAuthClient.o \
		   iscsiAuthClientGlue.o \
		   md5.o \
		   iscsi-crc.o

include $(TOPDIR)/Rules.make
