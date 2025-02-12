/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on Rusty Russell's IPv6 MASQUERADE target. Development of IPv6
 * NAT funded by Astaro.
 */

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/netdevice.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_nat.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/netfilter/ipv6/nf_nat_masquerade.h>

#define MAX_WORK_COUNT	16

static atomic_t v6_worker_count;

static int
nat_ipv6_dev_get_saddr(struct net *net, const struct net_device *dev,
		       const struct in6_addr *daddr, unsigned int srcprefs,
		       struct in6_addr *saddr)
{
#ifdef CONFIG_IPV6_MODULE
	const struct nf_ipv6_ops *v6_ops = nf_get_ipv6_ops();

	if (!v6_ops)
		return -EHOSTUNREACH;

	return v6_ops->dev_get_saddr(net, dev, daddr, srcprefs, saddr);
#else
	return ipv6_dev_get_saddr(net, dev, daddr, srcprefs, saddr);
#endif
}

unsigned int
nf_nat_masquerade_ipv6(struct sk_buff *skb, const struct nf_nat_range2 *range,
		       const struct net_device *out)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn_nat *nat;
	struct in6_addr src;
	struct nf_conn *ct;
	struct nf_nat_range2 newrange;

	ct = nf_ct_get(skb, &ctinfo);
	WARN_ON(!(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED ||
			 ctinfo == IP_CT_RELATED_REPLY)));

	if (nat_ipv6_dev_get_saddr(nf_ct_net(ct), out,
				   &ipv6_hdr(skb)->daddr, 0, &src) < 0)
		return NF_DROP;

	nat = nf_ct_nat_ext_add(ct);
	if (nat)
		nat->masq_index = out->ifindex;

	newrange.flags		= range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.in6	= src;
	newrange.max_addr.in6	= src;
	newrange.min_proto	= range->min_proto;
	newrange.max_proto	= range->max_proto;

	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_SRC);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6);

static int device_cmp(struct nf_conn *ct, void *ifindex)
{
	const struct nf_conn_nat *nat = nfct_nat(ct);

	if (!nat)
		return 0;
	if (nf_ct_l3num(ct) != NFPROTO_IPV6)
		return 0;
	return nat->masq_index == (int)(long)ifindex;
}

static int masq_device_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	const struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);

	if (event == NETDEV_DOWN)
		nf_ct_iterate_cleanup_net(net, device_cmp,
					  (void *)(long)dev->ifindex, 0, 0);

	return NOTIFY_DONE;
}

static struct notifier_block masq_dev_notifier = {
	.notifier_call	= masq_device_event,
};

struct masq_dev_work {
	struct work_struct work;
	struct net *net;
	struct in6_addr addr;
	int ifindex;
};

static int inet_cmp(struct nf_conn *ct, void *work)
{
	struct masq_dev_work *w = (struct masq_dev_work *)work;
	struct nf_conntrack_tuple *tuple;

	if (!device_cmp(ct, (void *)(long)w->ifindex))
		return 0;

	tuple = &ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	return ipv6_addr_equal(&w->addr, &tuple->dst.u3.in6);
}

static void iterate_cleanup_work(struct work_struct *work)
{
	struct masq_dev_work *w;

	w = container_of(work, struct masq_dev_work, work);

	nf_ct_iterate_cleanup_net(w->net, inet_cmp, (void *)w, 0, 0);

	put_net(w->net);
	kfree(w);
	atomic_dec(&v6_worker_count);
	module_put(THIS_MODULE);
}

/* ipv6 inet notifier is an atomic notifier, i.e. we cannot
 * schedule.
 *
 * Unfortunately, nf_ct_iterate_cleanup_net can run for a long
 * time if there are lots of conntracks and the system
 * handles high softirq load, so it frequently calls cond_resched
 * while iterating the conntrack table.
 *
 * So we defer nf_ct_iterate_cleanup_net walk to the system workqueue.
 *
 * As we can have 'a lot' of inet_events (depending on amount
 * of ipv6 addresses being deleted), we also need to add an upper
 * limit to the number of queued work items.
 */
static int masq_inet6_event(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = ptr;
	const struct net_device *dev;
	struct masq_dev_work *w;
	struct net *net;

	if (event != NETDEV_DOWN ||
	    atomic_read(&v6_worker_count) >= MAX_WORK_COUNT)
		return NOTIFY_DONE;

	dev = ifa->idev->dev;
	net = maybe_get_net(dev_net(dev));
	if (!net)
		return NOTIFY_DONE;

	if (!try_module_get(THIS_MODULE))
		goto err_module;

	w = kmalloc(sizeof(*w), GFP_ATOMIC);
	if (w) {
		atomic_inc(&v6_worker_count);

		INIT_WORK(&w->work, iterate_cleanup_work);
		w->ifindex = dev->ifindex;
		w->net = net;
		w->addr = ifa->addr;
		schedule_work(&w->work);

		return NOTIFY_DONE;
	}

	module_put(THIS_MODULE);
 err_module:
	put_net(net);
	return NOTIFY_DONE;
}

static struct notifier_block masq_inet6_notifier = {
	.notifier_call	= masq_inet6_event,
};

static int masq_refcnt;
static DEFINE_MUTEX(masq_mutex);

int nf_nat_masquerade_ipv6_register_notifier(void)
{
	int ret = 0;

	mutex_lock(&masq_mutex);
	/* check if the notifier is already set */
	if (++masq_refcnt > 1)
		goto out_unlock;

	ret = register_netdevice_notifier(&masq_dev_notifier);
	if (ret)
		goto err_dec;

	ret = register_inet6addr_notifier(&masq_inet6_notifier);
	if (ret)
		goto err_unregister;

	mutex_unlock(&masq_mutex);
	return ret;

err_unregister:
	unregister_netdevice_notifier(&masq_dev_notifier);
err_dec:
	masq_refcnt--;
out_unlock:
	mutex_unlock(&masq_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6_register_notifier);

void nf_nat_masquerade_ipv6_unregister_notifier(void)
{
	mutex_lock(&masq_mutex);
	/* check if the notifier still has clients */
	if (--masq_refcnt > 0)
		goto out_unlock;

	unregister_inet6addr_notifier(&masq_inet6_notifier);
	unregister_netdevice_notifier(&masq_dev_notifier);
out_unlock:
	mutex_unlock(&masq_mutex);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6_unregister_notifier);
