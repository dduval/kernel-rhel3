/*
 * IPVS:        Weighted Least-Connection Scheduling module
 *
 * Version:     $Id: ip_vs_wlc.c,v 1.10 2002/03/25 12:44:35 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Peter Kese <peter.kese@ijs.si>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *     Wensong Zhang            :     changed the ip_vs_wlc_schedule to return dest
 *     Wensong Zhang            :     changed to use the inactconns in scheduling
 *     Wensong Zhang            :     changed some comestics things for debugging
 *     Wensong Zhang            :     changed for the d-linked destination list
 *     Wensong Zhang            :     added the ip_vs_wlc_update_svc
 *     Wensong Zhang            :     added any dest with weight=0 is quiesced
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include <net/ip_vs.h>


static int
ip_vs_wlc_init_svc(struct ip_vs_service *svc)
{
	return 0;
}


static int
ip_vs_wlc_done_svc(struct ip_vs_service *svc)
{
	return 0;
}


static int
ip_vs_wlc_update_svc(struct ip_vs_service *svc)
{
	return 0;
}


static inline unsigned int
ip_vs_wlc_dest_overhead(struct ip_vs_dest *dest)
{
	/*
	 * We think the overhead of processing active connections is 256
	 * times higher than that of inactive connections in average. (This
	 * 256 times might not be accurate, we will change it later) We
	 * use the following formula to estimate the overhead now:
	 *		  dest->activeconns*256 + dest->inactconns
	 */
	return (atomic_read(&dest->activeconns) << 8) +
		atomic_read(&dest->inactconns);
}


/*
 *    Weighted Least Connection scheduling
 */
static struct ip_vs_dest *
ip_vs_wlc_schedule(struct ip_vs_service *svc, struct iphdr *iph)
{
	register struct list_head *l, *e;
	struct ip_vs_dest *dest, *least;
	unsigned int loh, doh;

	IP_VS_DBG(6, "ip_vs_wlc_schedule(): Scheduling...\n");

	/*
	 * We calculate the load of each dest server as follows:
	 *		  (dest overhead) / dest->weight
	 *
	 * Remember -- no floats in kernel mode!!!
	 * The comparison of h1*w2 > h2*w1 is equivalent to that of
	 *		  h1/w1 > h2/w2
	 * if every weight is larger than zero.
	 *
	 * The server with weight=0 is quiesced and will not receive any
	 * new connections.
	 */

	l = &svc->destinations;
	for (e=l->next; e!=l; e=e->next) {
		least = list_entry(e, struct ip_vs_dest, n_list);
		if (atomic_read(&least->weight) > 0) {
			loh = ip_vs_wlc_dest_overhead(least);
			goto nextstage;
		}
	}
	return NULL;

	/*
	 *    Find the destination with the least load.
	 */
  nextstage:
	for (e=e->next; e!=l; e=e->next) {
		dest = list_entry(e, struct ip_vs_dest, n_list);

		doh = ip_vs_wlc_dest_overhead(dest);
		if (loh * atomic_read(&dest->weight) >
		    doh * atomic_read(&least->weight)) {
			least = dest;
			loh = doh;
		}
	}

	IP_VS_DBG(6, "WLC: server %u.%u.%u.%u:%u "
		  "activeconns %d refcnt %d weight %d overhead %d\n",
		  NIPQUAD(least->addr), ntohs(least->port),
		  atomic_read(&least->activeconns),
		  atomic_read(&least->refcnt),
		  atomic_read(&least->weight), loh);

	return least;
}


static struct ip_vs_scheduler ip_vs_wlc_scheduler =
{
	{0},			/* n_list */
	"wlc",			/* name */
	ATOMIC_INIT(0),         /* refcnt */
	THIS_MODULE,		/* this module */
	ip_vs_wlc_init_svc,	/* service initializer */
	ip_vs_wlc_done_svc,	/* service done */
	ip_vs_wlc_update_svc,	/* service updater */
	ip_vs_wlc_schedule,	/* select a server from the destination list */
};


static int __init ip_vs_wlc_init(void)
{
	INIT_LIST_HEAD(&ip_vs_wlc_scheduler.n_list);
	return register_ip_vs_scheduler(&ip_vs_wlc_scheduler);
}

static void __exit ip_vs_wlc_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_wlc_scheduler);
}

module_init(ip_vs_wlc_init);
module_exit(ip_vs_wlc_cleanup);
MODULE_LICENSE("GPL");
