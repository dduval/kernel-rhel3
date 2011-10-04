#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := kernel.o

export-objs = signal.o sys.o kmod.o context.o ksyms.o pm.o exec_domain.o \
	      printk.o cpufreq.o profile.o module.o fork.o

obj-y     = sched.o dma.o fork.o exec_domain.o panic.o printk.o profile.o \
	    module.o exit.o itimer.o info.o time.o softirq.o resource.o \
	    sysctl.o acct.o capability.o ptrace.o timer.o user.o \
	    signal.o sys.o kmod.o context.o \
	    futex.o pid.o kksymoops.o

obj-$(CONFIG_UID16) += uid16.o
obj-$(CONFIG_MODULES) += ksyms.o
obj-$(CONFIG_COMPAT) += compat.o
obj-$(CONFIG_PM) += pm.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
obj-$(CONFIG_IKCONFIG) += configs.o
obj-$(CONFIG_KALLSYMS) += kallsyms.o

ifneq ($(CONFIG_IA64),y)
# According to Alan Modra <alan@linuxcare.com.au>, the -fno-omit-frame-pointer is
# needed for x86 only.  Why this used to be enabled for all architectures is beyond
# me.  I suspect most platforms don't need this, but until we know that for sure
# I turn this off for IA-64 only.  Andreas Schwab says it's also needed on m68k
# to get a correct value for the wait-channel (WCHAN in ps). --davidm
#
# Some gcc's are building so that O(1) scheduler is triple faulting if we 
# build -O2. (Turns out to be a CPU issue. Update your microcode if you hit it)
#
CFLAGS_sched.o := $(PROFILING) -fno-omit-frame-pointer
endif

include $(TOPDIR)/Rules.make

configs.o: $(TOPDIR)/scripts/mkconfigs configs.c
	$(CC) $(CFLAGS) $(CFLAGS_KERNEL) -DEXPORT_SYMTAB -c -o configs.o configs.c

$(TOPDIR)/scripts/mkconfigs: $(TOPDIR)/scripts/mkconfigs.c
	$(HOSTCC) $(HOSTCFLAGS) -o $(TOPDIR)/scripts/mkconfigs $(TOPDIR)/scripts/mkconfigs.c

configs.c: $(TOPDIR)/.config $(TOPDIR)/scripts/mkconfigs
	$(TOPDIR)/scripts/mkconfigs $(TOPDIR)/.config configs.c

