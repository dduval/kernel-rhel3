# arch/ppc64/oprofile/Makefile

O_TARGET:=oprofile.o
obj-m := $(O_TARGET)
obj-y := init.o timer_int.o \
         $(addprefix ../../../drivers/oprofile/, \
           oprof.o cpu_buffer.o buffer_sync.o \
           event_buffer.o oprofile_files.o \
           oprofilefs.o oprofile_stats.o )

include $(TOPDIR)/Rules.make
