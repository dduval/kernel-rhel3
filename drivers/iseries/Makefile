#
# Makefile for the iSeries-specific device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

# The target object and module list name.

# O_TARGET	:= macintosh.o

O_TARGET  := iseries.o

# Objects that export symbols.

# export-objs	:= adb.o rtc.o mac_hid.o via-pmu.o

export-objs := veth.o viocons.o viotape.o viodasd.o viocd.o viopath.o

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

obj-$(CONFIG_VETH) += veth.o
obj-$(CONFIG_VIOCONS) += viocons.o
obj-$(CONFIG_VIOPATH) += viopath.o
obj-$(CONFIG_VIOTAPE) += viotape.o
obj-$(CONFIG_VIODASD) += viodasd.o
obj-$(CONFIG_VIOCD)   += viocd.o

# The global Rules.make.

include $(TOPDIR)/Rules.make

