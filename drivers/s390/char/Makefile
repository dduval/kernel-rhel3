#
# S/390 character devices
#

O_TARGET := s390-char.o

list-multi	:= 	tub3270.o \
			tape390.o

export-objs	:=	sclp.o \
			tape_core.o \
			tape_devmap.o \
			tape_std.o

tub3270-objs := tuball.o tubfs.o tubtty.o \
                     tubttyaid.o tubttybld.o tubttyscl.o \
                     tubttyrcl.o tubttysiz.o

tape-$(CONFIG_S390_TAPE_BLOCK) += tape_block.o
tape-objs := tape_core.o tape_devmap.o tape_proc.o tape_std.o tape_char.o \
	$(sort $(tape-y))
obj-$(CONFIG_S390_TAPE) += tape390.o
obj-$(CONFIG_S390_TAPE_34XX) += tape_34xx.o

obj-y += ctrlchar.o
obj-$(CONFIG_TN3215) += con3215.o
obj-$(CONFIG_SCLP) += sclp.o sclp_rw.o
obj-$(CONFIG_SCLP_TTY) += sclp_tty.o
obj-$(CONFIG_SCLP_CONSOLE) += sclp_con.o
obj-$(CONFIG_SCLP_CPI) += sclp_cpi.o
obj-$(CONFIG_TN3270) += tub3270.o

include $(TOPDIR)/Rules.make

tub3270.o: $(tub3270-objs)
	$(LD) -r -o $@ $(tub3270-objs)

tape390.o:	$(tape-objs)
	$(LD) -r -o $@ $(tape-objs)
