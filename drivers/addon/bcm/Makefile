#
# Makefile for linux/drivers/addon/bcm/Linux
#

O_TARGET := bcm5820_foo.o

EXTRA_CFLAGS += -DUBSEC_5820 -DGOTOSLEEP -DMULTIPLE_DEVICES -DLINUX -DLINUX_DEVICE


obj-$(CONFIG_CRYPTO_BROADCOM)	+= bcm5820.o 


BCMOBJS=	              \
	chipinfo.o	\
	crypto.o	\
	device.o	\
	dispatch.o	\
	hash.o		\
	math.o		\
	param.o		\
	pkey.o		\
	rng.o		\
	selftest.o	\
	snmp.o		\
	sslarc4.o	\
	sslcipher.o	\
	sslmac.o	\
	tlsmac.o	\
	ubsciphr.o	\
	ubsdh.o		\
	ubsdsa.o	\
	ubsinit.o	\
	ubsint.o	\
	ubskey.o	\
	ubskeytest.o	\
	ubsmath.o	\
	ubsmd5.o	\
	ubsnorm.o	\
	ubsrng.o	\
	ubsrsa.o	\
	ubssha1.o	\
	ubsssl.o	\
	ubsstats.o	\
	ubsstruc.o	\
	ubstest.o	\
	ubsutils.o	\
	memory.o	\
	utils.o
	

include $(TOPDIR)/Rules.make

bcm5820.o: $(BCMOBJS)
	$(LD)  -o $@ -r $(BCMOBJS)
