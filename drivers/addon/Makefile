# File: drivers/addon/Makefile
#
# Makefile for the device drivers that are not in the main tree but added 
# to the Red Hat linux kernel
#

obj-y           :=
obj-m           :=
obj-n           :=
obj-            :=

mod-subdirs     := cipe qla2200 aep bcm megarac ips_60026 cmpci_564

O_TARGET := addon.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs     :=
list-multi	:=


subdir-$(CONFIG_CIPE) += cipe
subdir-$(CONFIG_MEGARAC) += megarac
subdir-$(CONFIG_SCSI_IPS) += ips_60026
subdir-$(CONFIG_SOUND_CMPCI) += cmpci_564

subdir-$(CONFIG_CRYPTO_AEP) += aep
subdir-$(CONFIG_CRYPTO_BROADCOM) += bcm
subdir-$(CONFIG_FC_QLA2100) += qla2200
subdir-$(CONFIG_FC_QLA2200) += qla2200
subdir-$(CONFIG_FC_QLA2300) += qla2200





include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s *~

