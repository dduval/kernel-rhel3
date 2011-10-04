export-objs := stubs.o

AUDIT_OBJS := control.o message.o args.o fileset.o \
		syscall.o syscall-$(ARCH).o filter.o sysctl.o \
		ioctl.o
O_TARGET := audit-stubs.o

ifeq ($(CONFIG_AUDIT),y)
obj-y := $(AUDIT_OBJS)
endif

ifeq ($(CONFIG_AUDIT),m)
obj-y := stubs.o
obj-m := audit.o
endif

list-multi	:= audit.o
audit-objs	:= $(AUDIT_OBJS)

include $(TOPDIR)/Rules.make

audit.o: $(audit-objs)
	$(LD) -r -o $@ $(audit-objs)
