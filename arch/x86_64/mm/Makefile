#
# Makefile for the linux i386-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
obj-y	 := init.o fault.o ioremap.o extable.o modutil.o pageattr.o
obj-$(CONFIG_DISCONTIGMEM) += numa.o
obj-$(CONFIG_K8_NUMA) += k8topology.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o

export-objs := pageattr.o numa.o

include $(TOPDIR)/Rules.make
