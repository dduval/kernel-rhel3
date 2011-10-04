#
# Makefile for the Qlogic fibre channel drivers
#
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#

SRC_FILES=exioct.h ql2200_fw.h \
qla_cfg.c qla_fo.cfg qla_mbx.c \
qla_version.h exioctln.h ql2200ip_fw.h qla2300.c \
qla_cfg.h     qla_fo.h      qla_mbx.h       qlfo.h \
inioct.h     ql2300_fw.h    qla2300_conf.c \
qla_cfgln.c   qla_gbl.h     qlfolimits.h \
listops.h    ql2300ip_fw.h  qla2x00.c \
qla_debug.h   qla_inioct.c  qla_opts.h \
qlfoln.h     qla2100.c      qla2x00.h \
qla_devtbl.h  qla_ip.c      qla_settings.h \
ql2100_fw.h  qla2200.c      qla2x00_ioctl.c \
qla_fo.c      qla_ip.h      qla_vendor.c

O_TARGET := qla.o
EXTRA_CFLAGS := -I../../scsi
obj-$(CONFIG_FC_QLA2100)	+= qla2100_60702RH2.o
obj-$(CONFIG_FC_QLA2200)	+= qla2200_60702RH2.o
obj-$(CONFIG_FC_QLA2300)	+= qla2300_60702RH2.o

include $(TOPDIR)/Rules.make

qla2100_60702RH2.o: $(SRC_FILES) 
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c qla2100.c -o $@

qla2200_60702RH2.o: $(SRC_FILES) 
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c qla2200.c -o $@

qla2300_60702RH2.o: $(SRC_FILES) 
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c qla2300.c -o $@
