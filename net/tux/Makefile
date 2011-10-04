#
# Makefile for TUX
#

O_TARGET := tux.o
MOD_LIST_NAME := NET_MODULES

obj-y := accept.o input.o userspace.o cachemiss.o output.o \
	redirect.o postpone.o logger.o proto_http.o proto_ftp.o \
	proc.o main.o mod.o abuf.o times.o directory.o gzip.o

obj-$(CONFIG_TUX_EXTCGI) += cgi.o extcgi.o
obj-m   := $(O_TARGET)

include $(TOPDIR)/Rules.make

