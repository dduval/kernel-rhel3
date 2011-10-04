#
# Makefile for the Qlogic fibre channel drivers
#
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#

SRC_FILES=exioct.h exioctln.h inioct.h listops.h \
ql2100_fw.h ql2200_fw.h ql2200ip_fw.h ql2300flx_fw.h \
ql2300ipx_fw.h ql2322flx_fw.h ql2322ipx_fw.h qla2100.c \
qla2200.c qla2200_conf.c qla2300.c qla2300_conf.c qla2x00.c \
qla2x00.h qla2x00_ioctl.c qla_cfg.c qla_cfg.h qla_cfgln.c \
qla_debug.h qla_devtbl.h qla_fo.c qla_fo.cfg qla_fo.h \
qla_gbl.h qla_gs.c qla_gs.h qla_inioct.c qla_inline.h \
qla_ip.c qla_ip.h qla_mbx.c qla_mbx.h qla_opts.h \
qla_ppc64.c qla_settings.h qla_vendor.c \
qla_version.h qlfo.h qlfolimits.h qlfoln.h

O_TARGET := qla.o
EXTRA_CFLAGS := -I../../scsi
obj-$(CONFIG_FC_QLA2100)	+= qla2100_70003RH1.o
obj-$(CONFIG_FC_QLA2200)	+= qla2200_70003RH1.o
obj-$(CONFIG_FC_QLA2300)	+= qla2300_70003RH1.o

include $(TOPDIR)/Rules.make

qla2100_70003RH1.o: $(SRC_FILES) 
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c qla2100.c -o $@

qla2200_70003RH1.o: $(SRC_FILES) 
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c qla2200.c -o $@

qla2300_70003RH1.o: $(SRC_FILES) 
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c qla2300.c -o $@
