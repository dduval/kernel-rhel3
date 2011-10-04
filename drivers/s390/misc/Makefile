#
# S/390 miscellaneous devices
#

O_TARGET := s390-misc.o

export-objs += chandev.o

list-multi := z90crypt.o
z90crypt_mod-objs := z90main.o z90hardware.o
obj-$(CONFIG_Z90CRYPT) += z90crypt.o

obj-$(CONFIG_CHANDEV) += chandev.o

include $(TOPDIR)/Rules.make

z90crypt.o: $(z90crypt_mod-objs)
	$(LD) -r -o $@ $(z90crypt_mod-objs)
