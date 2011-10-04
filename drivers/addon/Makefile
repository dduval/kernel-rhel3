# File: drivers/addon/Makefile
#
# Makefile for the device drivers that are not in the main tree but added 
# to the Red Hat linux kernel
#

obj-y           :=
obj-m           :=
obj-n           :=
obj-            :=

mod-subdirs     := cipe megarac ips_70015 cmpci_564 aep bcm \
		   qla2200 qla2200_70003RH1 lpfc lpfc_703 \
		   megaraid_2106 fusion_20511 aacraid_10102 \
		   iscsi_sfnet 3w-xxxx_10200033

O_TARGET := addon.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs     :=
list-multi	:=


subdir-$(CONFIG_BLK_DEV_3W_XXXX_RAID) += 3w-xxxx_10200033
subdir-$(CONFIG_CIPE) += cipe
subdir-$(CONFIG_MEGARAC) += megarac
subdir-$(CONFIG_SCSI_IPS) += ips_70015
subdir-$(CONFIG_SOUND_CMPCI) += cmpci_564
subdir-$(CONFIG_CRYPTO_AEP) += aep
subdir-$(CONFIG_CRYPTO_BROADCOM) += bcm

subdir-$(CONFIG_FC_QLA2100) += qla2200 qla2200_70003RH1
subdir-$(CONFIG_FC_QLA2200) += qla2200 qla2200_70003RH1
subdir-$(CONFIG_FC_QLA2300) += qla2200 qla2200_70003RH1
subdir-$(CONFIG_SCSI_LPFC) += lpfc
subdir-$(CONFIG_SCSI_LPFC) += lpfc_703
subdir-$(CONFIG_SCSI_IPR) += ipr
subdir-$(CONFIG_SCSI_MEGARAID2) += megaraid_2106
subdir-$(CONFIG_FUSION) += fusion_20511
subdir-$(CONFIG_SCSI_AACRAID) += aacraid_10102
subdir-$(CONFIG_SCSI_ISCSI) += iscsi_sfnet


include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s *~

