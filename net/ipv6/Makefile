#
# Makefile for the Linux TCP/IP (INET6) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#


O_TARGET := ipv6.o

export-objs :=	ipv6_syms.o

obj-y :=	af_inet6.o anycast.o ip6_output.o ip6_input.o addrconf.o sit.o \
		route.o ip6_fib.o ipv6_sockglue.o ndisc.o udp.o raw.o \
		protocol.o icmp.o mcast.o reassembly.o tcp_ipv6.o \
		exthdrs.o sysctl_net_ipv6.o datagram.o proc.o \
		ip6_flowlabel.o ipv6_syms.o

obj-$(CONFIG_XFRM) += xfrm6_policy.o xfrm6_state.o xfrm6_input.o

obj-m  := $(O_TARGET)

obj-$(CONFIG_INET6_AH) += ah6.o
obj-$(CONFIG_INET6_ESP) += esp6.o
obj-$(CONFIG_INET6_IPCOMP) += ipcomp6.o

obj-$(CONFIG_IPV6_TUNNEL) += ip6_tunnel.o

include $(TOPDIR)/Rules.make
