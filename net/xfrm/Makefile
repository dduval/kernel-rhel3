#
# Makefile for the XFRM subsystem.
#

O_TARGET := xfrm.o

export-objs = xfrm_export.o

obj-$(CONFIG_XFRM) := xfrm_policy.o xfrm_state.o xfrm_input.o xfrm_algo.o xfrm_output.o \
	xfrm_export.o
obj-$(CONFIG_XFRM_USER) += xfrm_user.o

include $(TOPDIR)/Rules.make
