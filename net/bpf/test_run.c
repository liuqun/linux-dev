/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/bpf.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/sched/signal.h>
#include <net/sock.h>
#include <net/tcp.h>

static int bpf_test_run(struct bpf_prog *prog, void *ctx, u32 repeat,
			u32 *retval, u32 *time)
{
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE] = { 0 };
	enum bpf_cgroup_storage_type stype;
	u64 time_start, time_spent = 0;
	int ret = 0;
	u32 i;

	for_each_cgroup_storage_type(stype) {
		storage[stype] = bpf_cgroup_storage_alloc(prog, stype);
		if (IS_ERR(storage[stype])) {
			storage[stype] = NULL;
			for_each_cgroup_storage_type(stype)
				bpf_cgroup_storage_free(storage[stype]);
			return -ENOMEM;
		}
	}

	if (!repeat)
		repeat = 1;

	rcu_read_lock();
	preempt_disable();
	time_start = ktime_get_ns();
	for (i = 0; i < repeat; i++) {
		bpf_cgroup_storage_set(storage);
		*retval = BPF_PROG_RUN(prog, ctx);

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		if (need_resched()) {
			time_spent += ktime_get_ns() - time_start;
			preempt_enable();
			rcu_read_unlock();

			cond_resched();

			rcu_read_lock();
			preempt_disable();
			time_start = ktime_get_ns();
		}
	}
	time_spent += ktime_get_ns() - time_start;
	preempt_enable();
	rcu_read_unlock();

	do_div(time_spent, repeat);
	*time = time_spent > U32_MAX ? U32_MAX : (u32)time_spent;

	for_each_cgroup_storage_type(stype)
		bpf_cgroup_storage_free(storage[stype]);

	return ret;
}

static int bpf_test_finish(const union bpf_attr *kattr,
			   union bpf_attr __user *uattr, const void *data,
			   u32 size, u32 retval, u32 duration)
{
	void __user *data_out = u64_to_user_ptr(kattr->test.data_out);
	int err = -EFAULT;
	u32 copy_size = size;

	/* Clamp copy if the user has provided a size hint, but copy the full
	 * buffer if not to retain old behaviour.
	 */
	if (kattr->test.data_size_out &&
	    copy_size > kattr->test.data_size_out) {
		copy_size = kattr->test.data_size_out;
		err = -ENOSPC;
	}

	if (data_out && copy_to_user(data_out, data, copy_size))
		goto out;
	if (copy_to_user(&uattr->test.data_size_out, &size, sizeof(size)))
		goto out;
	if (copy_to_user(&uattr->test.retval, &retval, sizeof(retval)))
		goto out;
	if (copy_to_user(&uattr->test.duration, &duration, sizeof(duration)))
		goto out;
	if (err != -ENOSPC)
		err = 0;
out:
	return err;
}

static void *bpf_test_init(const union bpf_attr *kattr, u32 size,
			   u32 headroom, u32 tailroom)
{
	void __user *data_in = u64_to_user_ptr(kattr->test.data_in);
	void *data;

	if (size < ETH_HLEN || size > PAGE_SIZE - headroom - tailroom)
		return ERR_PTR(-EINVAL);

	data = kzalloc(size + headroom + tailroom, GFP_USER);
	if (!data)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(data + headroom, data_in, size)) {
		kfree(data);
		return ERR_PTR(-EFAULT);
	}
	return data;
}

int bpf_prog_test_run_skb(struct bpf_prog *prog, const union bpf_attr *kattr,
			  union bpf_attr __user *uattr)
{
	bool is_l2 = false, is_direct_pkt_access = false;
	u32 size = kattr->test.data_size_in;
	u32 repeat = kattr->test.repeat;
	u32 retval, duration;
	int hh_len = ETH_HLEN;
	struct sk_buff *skb;
	struct sock *sk;
	void *data;
	int ret;

	data = bpf_test_init(kattr, size, NET_SKB_PAD + NET_IP_ALIGN,
			     SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (prog->type) {
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
		is_l2 = true;
		/* fall through */
	case BPF_PROG_TYPE_LWT_IN:
	case BPF_PROG_TYPE_LWT_OUT:
	case BPF_PROG_TYPE_LWT_XMIT:
		is_direct_pkt_access = true;
		break;
	default:
		break;
	}

	sk = kzalloc(sizeof(struct sock), GFP_USER);
	if (!sk) {
		kfree(data);
		return -ENOMEM;
	}
	sock_net_set(sk, current->nsproxy->net_ns);
	sock_init_data(NULL, sk);

	skb = build_skb(data, 0);
	if (!skb) {
		kfree(data);
		kfree(sk);
		return -ENOMEM;
	}
	skb->sk = sk;

	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
	__skb_put(skb, size);
	skb->protocol = eth_type_trans(skb, current->nsproxy->net_ns->loopback_dev);
	skb_reset_network_header(skb);

	if (is_l2)
		__skb_push(skb, hh_len);
	if (is_direct_pkt_access)
		bpf_compute_data_pointers(skb);
	ret = bpf_test_run(prog, skb, repeat, &retval, &duration);
	if (ret) {
		kfree_skb(skb);
		kfree(sk);
		return ret;
	}
	if (!is_l2) {
		if (skb_headroom(skb) < hh_len) {
			int nhead = HH_DATA_ALIGN(hh_len - skb_headroom(skb));

			if (pskb_expand_head(skb, nhead, 0, GFP_USER)) {
				kfree_skb(skb);
				kfree(sk);
				return -ENOMEM;
			}
		}
		memset(__skb_push(skb, hh_len), 0, hh_len);
	}

	size = skb->len;
	/* bpf program can never convert linear skb to non-linear */
	if (WARN_ON_ONCE(skb_is_nonlinear(skb)))
		size = skb_headlen(skb);
	ret = bpf_test_finish(kattr, uattr, skb->data, size, retval, duration);
	kfree_skb(skb);
	kfree(sk);
	return ret;
}

int bpf_prog_test_run_xdp(struct bpf_prog *prog, const union bpf_attr *kattr,
			  union bpf_attr __user *uattr)
{
	u32 size = kattr->test.data_size_in;
	u32 repeat = kattr->test.repeat;
	struct netdev_rx_queue *rxqueue;
	struct xdp_buff xdp = {};
	u32 retval, duration;
	void *data;
	int ret;

	data = bpf_test_init(kattr, size, XDP_PACKET_HEADROOM + NET_IP_ALIGN, 0);
	if (IS_ERR(data))
		return PTR_ERR(data);

	xdp.data_hard_start = data;
	xdp.data = data + XDP_PACKET_HEADROOM + NET_IP_ALIGN;
	xdp.data_meta = xdp.data;
	xdp.data_end = xdp.data + size;

	rxqueue = __netif_get_rx_queue(current->nsproxy->net_ns->loopback_dev, 0);
	xdp.rxq = &rxqueue->xdp_rxq;

	ret = bpf_test_run(prog, &xdp, repeat, &retval, &duration);
	if (ret)
		goto out;
	if (xdp.data != data + XDP_PACKET_HEADROOM + NET_IP_ALIGN ||
	    xdp.data_end != xdp.data + size)
		size = xdp.data_end - xdp.data;
	ret = bpf_test_finish(kattr, uattr, xdp.data, size, retval, duration);
out:
	kfree(data);
	return ret;
}

int bpf_prog_test_run_flow_dissector(struct bpf_prog *prog,
				     const union bpf_attr *kattr,
				     union bpf_attr __user *uattr)
{
	u32 size = kattr->test.data_size_in;
	u32 repeat = kattr->test.repeat;
	struct bpf_flow_keys flow_keys;
	u64 time_start, time_spent = 0;
	struct bpf_skb_data_end *cb;
	u32 retval, duration;
	struct sk_buff *skb;
	struct sock *sk;
	void *data;
	int ret;
	u32 i;

	if (prog->type != BPF_PROG_TYPE_FLOW_DISSECTOR)
		return -EINVAL;

	data = bpf_test_init(kattr, size, NET_SKB_PAD + NET_IP_ALIGN,
			     SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
	if (IS_ERR(data))
		return PTR_ERR(data);

	sk = kzalloc(sizeof(*sk), GFP_USER);
	if (!sk) {
		kfree(data);
		return -ENOMEM;
	}
	sock_net_set(sk, current->nsproxy->net_ns);
	sock_init_data(NULL, sk);

	skb = build_skb(data, 0);
	if (!skb) {
		kfree(data);
		kfree(sk);
		return -ENOMEM;
	}
	skb->sk = sk;

	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
	__skb_put(skb, size);
	skb->protocol = eth_type_trans(skb,
				       current->nsproxy->net_ns->loopback_dev);
	skb_reset_network_header(skb);

	cb = (struct bpf_skb_data_end *)skb->cb;
	cb->qdisc_cb.flow_keys = &flow_keys;

	if (!repeat)
		repeat = 1;

	time_start = ktime_get_ns();
	for (i = 0; i < repeat; i++) {
		preempt_disable();
		rcu_read_lock();
		retval = __skb_flow_bpf_dissect(prog, skb,
						&flow_keys_dissector,
						&flow_keys);
		rcu_read_unlock();
		preempt_enable();

		if (need_resched()) {
			if (signal_pending(current))
				break;
			time_spent += ktime_get_ns() - time_start;
			cond_resched();
			time_start = ktime_get_ns();
		}
	}
	time_spent += ktime_get_ns() - time_start;
	do_div(time_spent, repeat);
	duration = time_spent > U32_MAX ? U32_MAX : (u32)time_spent;

	ret = bpf_test_finish(kattr, uattr, &flow_keys, sizeof(flow_keys),
			      retval, duration);

	kfree_skb(skb);
	kfree(sk);
	return ret;
}
