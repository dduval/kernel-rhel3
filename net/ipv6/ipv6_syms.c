#include <linux/config.h>
#include <linux/module.h>
#include <net/xfrm.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/ndisc.h>
#include <linux/icmpv6.h>

#ifdef CONFIG_XFRM
EXPORT_SYMBOL(xfrm6_rcv);
#endif
EXPORT_SYMBOL(ip6_find_1stfragopt);
EXPORT_SYMBOL(inet6_add_protocol);
EXPORT_SYMBOL(inet6_del_protocol);
EXPORT_SYMBOL(rt6_lookup);
EXPORT_SYMBOL(fl6_sock_lookup);
EXPORT_SYMBOL(ipv6_ext_hdr);
EXPORT_SYMBOL(ip6_append_data);
EXPORT_SYMBOL(ip6_flush_pending_frames);
EXPORT_SYMBOL(ip6_push_pending_frames);
EXPORT_SYMBOL(ipv6_addr_type);
EXPORT_SYMBOL(icmpv6_send);
EXPORT_SYMBOL(ndisc_mc_map);
EXPORT_SYMBOL(register_inet6addr_notifier);
EXPORT_SYMBOL(unregister_inet6addr_notifier);
EXPORT_SYMBOL(ip6_route_output);
#ifdef CONFIG_NETFILTER
EXPORT_SYMBOL(ip6_route_me_harder);
#endif
EXPORT_SYMBOL(ipv6_chk_addr);
