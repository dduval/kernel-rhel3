# File: drivers/addon/Makefile
#
# Makefile for the device drivers that are not in the main tree but added 
# to the Red Hat linux kernel
#

obj-y           :=
obj-m           :=
obj-n           :=
obj-            :=

mod-subdirs     := cipe megarac aep bcm qla2200 lpfc ipr iscsi_sfnet dell_rbu \
		dcdbas

O_TARGET := addon.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs     :=
list-multi	:=


subdir-$(CONFIG_CIPE) += cipe
subdir-$(CONFIG_MEGARAC) += megarac
subdir-$(CONFIG_CRYPTO_AEP) += aep
subdir-$(CONFIG_CRYPTO_BROADCOM) += bcm
subdir-$(CONFIG_FC_QLA2100) += qla2200
subdir-$(CONFIG_FC_QLA2200) += qla2200
subdir-$(CONFIG_FC_QLA2300) += qla2200
subdir-$(CONFIG_SCSI_LPFC) += lpfc
subdir-$(CONFIG_SCSI_IPR) += ipr
subdir-$(CONFIG_SCSI_ISCSI) += iscsi_sfnet
subdir-$(CONFIG_DELL_RBU) += dell_rbu
subdir-$(CONFIG_DCDBAS) += dcdbas

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s *~

