#
# Makefile for the kernel hardware sensors drivers.
#

MOD_LIST_NAME := SENSORS_MODULES
O_TARGET := sensor.o

export-objs	:= sensors.o

obj-$(CONFIG_SENSORS)		+= sensors.o
obj-$(CONFIG_SENSORS_ADM1021)	+= adm1021.o
obj-$(CONFIG_SENSORS_ADM1024)	+= adm1024.o
obj-$(CONFIG_SENSORS_ADM1025)	+= adm1025.o
obj-$(CONFIG_SENSORS_ADM9240)	+= adm9240.o
obj-$(CONFIG_SENSORS_BT869)	+= bt869.o
obj-$(CONFIG_SENSORS_DDCMON)	+= ddcmon.o
obj-$(CONFIG_SENSORS_DS1621)	+= ds1621.o
obj-$(CONFIG_SENSORS_EEPROM)	+= eeprom.o
obj-$(CONFIG_SENSORS_FSCPOS)	+= fscpos.o
obj-$(CONFIG_SENSORS_FSCSCY)	+= fscscy.o
obj-$(CONFIG_SENSORS_GL518SM)	+= gl518sm.o
obj-$(CONFIG_SENSORS_GL520SM)	+= gl520sm.o
obj-$(CONFIG_SENSORS_IT87)	+= it87.o
obj-$(CONFIG_SENSORS_LM75)	+= lm75.o
obj-$(CONFIG_SENSORS_LM78)	+= lm78.o
obj-$(CONFIG_SENSORS_LM80)	+= lm80.o
obj-$(CONFIG_SENSORS_LM87)	+= lm87.o
obj-$(CONFIG_SENSORS_LM87)	+= lm92.o
obj-$(CONFIG_SENSORS_MAXILIFE)	+= maxilife.o
obj-$(CONFIG_SENSORS_MTP008)	+= mtp008.o
obj-$(CONFIG_SENSORS_SIS5595)	+= sis5595.o
obj-$(CONFIG_SENSORS_SMSC47M1)	+= smsc47m1.o
obj-$(CONFIG_SENSORS_THMC50)	+= thmc50.o
obj-$(CONFIG_SENSORS_VIA686A)	+= via686a.o
obj-$(CONFIG_SENSORS_VT1211)	+= vt1211.o
obj-$(CONFIG_SENSORS_VT8231)	+= 
obj-$(CONFIG_SENSORS_W83781D)	+= w83781d.o

include $(TOPDIR)/Rules.make

