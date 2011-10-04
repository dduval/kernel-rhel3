#
# oprofile for x86-64.
# Just reuse the one from i386. The Hammer performance counters 
# are similar to Athlon.
#

O_TARGET:=oprofile.o
obj-m := $(O_TARGET)
obj-y = init.o timer_int.o \
	$(addprefix ../../../drivers/oprofile/, \
	oprof.o cpu_buffer.o buffer_sync.o \
	event_buffer.o oprofile_files.o \
	oprofilefs.o oprofile_stats.o )
 
ifeq ($(CONFIG_X86_LOCAL_APIC),y)
obj-y += nmi_int.o op_model_athlon.o
ifdef CONFIG_IA32E
obj-y += op_model_p4.o op_model_ppro.o
endif
endif
 
obj	:= .
INCL := $(obj)/op_counter.h $(obj)/op_x86_model.h

$(obj)/nmi_int.c: ${INCL}
	@ln -sf ../../i386/oprofile/nmi_int.c $(obj)/nmi_int.c
$(obj)/op_model_athlon.c: ${INCL}
	@ln -sf ../../i386/oprofile/op_model_athlon.c $(obj)/op_model_athlon.c
ifdef CONFIG_IA32E
$(obj)/op_model_p4.c: ${INCL}
	@ln -sf ../../i386/oprofile/op_model_p4.c $(obj)/op_model_p4.c
$(obj)/op_model_ppro.c: ${INCL}
	@ln -sf ../../i386/oprofile/op_model_ppro.c $(obj)/op_model_ppro.c
endif
$(obj)/init.c: ${INCL}
	@ln -sf ../../i386/oprofile/init.c $(obj)/init.c
$(obj)/timer_int.c: ${INCL}
	@ln -sf ../../i386/oprofile/timer_int.c $(obj)/timer_int.c
$(obj)/op_counter.h: 
	@ln -sf ../../i386/oprofile/op_counter.h $(obj)/op_counter.h
$(obj)/op_x86_model.h:
	@ln -sf ../../i386/oprofile/op_x86_model.h $(obj)/op_x86_model.h	
clean-files += op_x86_model.h op_counter.h timer_int.c init.c \
	       op_model_athlon.c nmi_int.c
ifdef CONFIG_IA32E
clean-files += op_model_p4.c op_model_ppro.c
endif

include $(TOPDIR)/Rules.make
