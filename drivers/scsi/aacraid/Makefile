# Adaptec aacraid

ifeq (${VERSION},2) # 2.x.x

ifeq (${PATCHLEVEL},2) # 2.2.x

CFILES_DRIVER=linit.c aachba.c commctrl.c comminit.c commsup.c \
	dpcsup.c rx.c sa.c rkt.c

IFILES_DRIVER=aacraid.h compat.h

ALL_SOURCE=${CFILES_DRIVER} ${IFILES_DRIVER} 

TARGET_OFILES=${CFILES_DRIVER:.c=.o}

ifndef GCCVERSION
GCCVERSION=2.96
endif

GCCMACHINE:=$(shell ls -d /usr/lib/gcc-lib/*/${GCCVERSION} | sed -n 1s@/${GCCVERSION}@@p)

INCS=-I. -I.. -I../../../include -I/usr/src/linux/include -I/usr/src/linux/drivers/scsi 
INCS=-nostdinc -I${GCCMACHINE}/${GCCVERSION}/include -I. -I..

WARNINGS= -w -Wall -Wno-unused -Wno-switch -Wno-missing-prototypes -Wno-implicit

COMMON_FLAGS=\
	-D__KERNEL__=1 -DUNIX -DCVLOCK_USE_SPINLOCK -DLINUX \
	-Wall -Wstrict-prototypes \
	${INCS} \
	${WARNINGS} \
	-O2 -fomit-frame-pointer

AACFLAGS=${COMMON_FLAGS} ${CFLAGS} ${COMMON_FLAGS} ${EXTRA_FLAGS}
COMPILE.c=${CC} ${AACFLAGS} ${TARGET_ARCH} -c

.SUFFIXES:
.SUFFIXES: .c .o .h .a

all: source ${TARGET_OFILES} aacraid.o

modules: all

source: ${ALL_SOURCE}

clean:
	rm *.o

aacraid.o: source ${TARGET_OFILES}
	ld -r -o $@ $(TARGET_OFILES)
	cp -r aacraid.o ../

endif # 2.2.x

ifeq (${PATCHLEVEL},4) # 2.4.x

EXTRA_CFLAGS	+= -I$(TOPDIR)/drivers/scsi ${EXTRA_FLAGS}

O_TARGET	:= aacraid.o
obj-m		:= $(O_TARGET)

obj-y		:= linit.o aachba.o commctrl.o comminit.o commsup.o \
		   dpcsup.o rx.o sa.o rkt.o

include $(TOPDIR)/Rules.make

endif # 2.4.x

ifeq (${PATCHLEVEL},6) # 2.6.x

obj-$(CONFIG_SCSI_AACRAID) := aacraid.o

aacraid-objs	:= linit.o aachba.o commctrl.o comminit.o commsup.o \
		   dpcsup.o rx.o sa.o rkt.o

EXTRA_CFLAGS	:= -Idrivers/scsi ${EXTRA_FLAGS}

endif # 2.6.x

endif # 2.x.x
