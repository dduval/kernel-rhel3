#
# drivers/net/wireless/Makefile
#
# Makefile for the Linux Wireless network device drivers.
#

O_TARGET := wireless_old_net.o

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

ori-objs	:= orinoco_cs.o orinoco.o hermes.o

# Things that need to export symbols

obj-$(CONFIG_PCMCIA_HERMES_OLD)	+= orinoco_old_cs.o


include $(TOPDIR)/Rules.make

orinoco_old_cs.o: $(ori-objs)
	$(LD) -r -o $@ $(ori-objs)

