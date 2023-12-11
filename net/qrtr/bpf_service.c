/* Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/soc/qcom/qmi.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include "bpf_service.h"
#include <linux/bpf.h>
#include "qrtr.h"

/* qrtr filter (based on eBPF) related declarations */
#define MAX_GID_SUPPORTED	16
#define QMI_HEADER_SIZE		sizeof(struct qmi_header)

/* filter argument to be passed while executing eBPF filter */
struct bpf_data {
	struct service_info svc_info;
	u16 pkt_type;
	u16 direction;
	unsigned char data[QMI_HEADER_SIZE];
	u32 gid_len;
	uid_t gid[MAX_GID_SUPPORTED];
	u32 dest_node;
} __packed;

#define BPF_DATA_SIZE	sizeof(struct bpf_data)

/* for service lookup for eBPF */
static RADIX_TREE(service_lookup, GFP_KERNEL);

/* mutex to lock service lookup */
static DEFINE_MUTEX(service_lookup_lock);

/* variable to hold bpf filter object */
static struct sk_filter __rcu *bpf_filter;

/**
 * Add service information (service id & instance id) to lookup table
 * with key as node & port id pair
 */
void qrtr_service_add(struct qrtr_ctrl_pkt *pkt)
{
	struct service_info *info;
	unsigned long key = 0;

	key = (u64)le32_to_cpu(pkt->server.node) << 32 |
			le32_to_cpu(pkt->server.port);
	mutex_lock(&service_lookup_lock);
	info = radix_tree_lookup(&service_lookup, key);
	if (!info) {
		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (info) {
			info->service_id = le32_to_cpu(pkt->server.service);
			info->instance_id = le32_to_cpu(pkt->server.instance);
			info->node_id = le32_to_cpu(pkt->server.node);
			radix_tree_insert(&service_lookup, key, info);
		} else {
			pr_err("%s svc<0x%x:0x%x> adding to lookup failed\n",
			       __func__, le32_to_cpu(pkt->server.service),
			       le32_to_cpu(pkt->server.instance));
		}
	}
	mutex_unlock(&service_lookup_lock);
}
EXPORT_SYMBOL(qrtr_service_add);

/* Get service information from service lookup table */
int qrtr_service_lookup(u32 node, u32 port, struct service_info **info)
{
	struct service_info *sinfo = NULL;
	unsigned long key = 0;
	int rc = -EINVAL;

	key = (u64)node << 32 | port;
	mutex_lock(&service_lookup_lock);
	sinfo = radix_tree_lookup(&service_lookup, key);
	mutex_unlock(&service_lookup_lock);
	if (sinfo) {
		*info = sinfo;
		rc = 0;
	}

	return rc;
}
EXPORT_SYMBOL(qrtr_service_lookup);

/* Remove service information from service lookup table */
void qrtr_service_remove(struct qrtr_ctrl_pkt *pkt)
{
	struct service_info *info;
	unsigned long key = 0;

	key = (u64)le32_to_cpu(pkt->server.node) << 32 |
			le32_to_cpu(pkt->server.port);
	mutex_lock(&service_lookup_lock);
	info = radix_tree_lookup(&service_lookup, key);
	kfree(info);
	radix_tree_delete(&service_lookup, key);
	mutex_unlock(&service_lookup_lock);
}
EXPORT_SYMBOL(qrtr_service_remove);

/* Remove all services from requested node */
void qrtr_service_node_remove(u32 src_node)
{
	struct radix_tree_iter iter;
	struct service_info *info;
	void __rcu **slot;
	u32 node_id;

	mutex_lock(&service_lookup_lock);
	radix_tree_for_each_slot(slot, &service_lookup, &iter, 0) {
		info = rcu_dereference(*slot);
		/**
		 * get node id from info structure & remove service
		 * info only for matching node_id
		 */
		node_id = info->node_id;
		if (node_id != src_node)
			continue;

		kfree(info);
		radix_tree_iter_delete(&service_lookup, &iter, slot);
	}
	mutex_unlock(&service_lookup_lock);
}
EXPORT_SYMBOL(qrtr_service_node_remove);

/**
 * Get eBPF filter object from eBPF framework using filter fd
 * passed by the user space program. QRTR will allow to attach
 * eBPF filter only for privileged user space program. This
 * attached filter then executed on different qrtr events.
 */
int qrtr_bpf_filter_attach(int ufd)
{
	struct sk_filter *filter = NULL;
	struct bpf_prog *prog = NULL;
	int rc = 0;

	/* return invalid error if fd is not valid */
	if (ufd < 0)
		return -EINVAL;

	/* return -EEXIST if ebpf filter is already attached */
	if (bpf_filter)
		return -EEXIST;

	if (!(in_egroup_p(AID_VENDOR_QRTR) ||
	      in_egroup_p(GLOBAL_ROOT_GID)))
		return -EPERM;

	prog = bpf_prog_get_type(ufd, BPF_PROG_TYPE_SOCKET_FILTER);
	if (prog) {
		pr_info("%s bpf filter with fd %d attached with qrtr\n",
			__func__, ufd);
		filter = kzalloc(sizeof(*bpf_filter), GFP_KERNEL);
		if (!filter)
			return -ENOMEM;
		filter->prog = prog;
		rcu_assign_pointer(bpf_filter, filter);
	} else {
		rc = -EFAULT;
	}

	return rc;
}
EXPORT_SYMBOL(qrtr_bpf_filter_attach);

/* Detach previous attached eBPF filter program */
int qrtr_bpf_filter_detach(void)
{
	struct sk_filter *filter = NULL;
	int rc = -EFAULT;

	rcu_read_lock();
	filter = rcu_dereference(bpf_filter);
	rcu_read_unlock();

	if (filter && filter->prog) {
		pr_info("%s bpf filter program detached\n",
			__func__);
		bpf_filter = NULL;
		bpf_prog_put(filter->prog);
		kfree(filter);
		rc = 0;
	}

	return rc;
}
EXPORT_SYMBOL(qrtr_bpf_filter_detach);

/**
 * This will populate argument structure for eBPF filter input and
 * execute filter for both data packet & new server control packet
 */
int qrtr_run_bpf_filter(struct sk_buff *skb, u32 service_id, u32 instance_id,
			u8 pkt_type, u32 dest_node)
{
	struct sk_buff *skb_bpf = NULL;
	struct group_info *group_info;
	struct bpf_data filter_arg;
	struct sk_filter *filter;
	int err = 0;
	kuid_t euid;
	kgid_t egid;
	uid_t kgid;
	int i = 0;

	/* populate filter argument with service & pkt type information */
	filter_arg.svc_info.service_id = service_id;
	filter_arg.svc_info.instance_id = instance_id;
	filter_arg.pkt_type = pkt_type;

	/* gid information is required only for data packet filtration */
	if (pkt_type == QRTR_TYPE_DATA) {
		/* Copy qmi header from original skbuff to bpf skbuff */
		skb_copy_bits(skb, 0, &filter_arg.data[0], QMI_HEADER_SIZE);

		/* Check effective group id of client */
		current_euid_egid(&euid, &egid);
		kgid = from_kgid(&init_user_ns, egid);
		filter_arg.gid[0] = kgid;

		/* Check supplimentary group id's of client */
		group_info = get_current_groups();
		for (i = 0; i < group_info->ngroups; i++) {
			if (i >= (MAX_GID_SUPPORTED - 1))
				break;
			egid = group_info->gid[i];
			filter_arg.gid[i + 1] = from_kgid(&init_user_ns, egid);
		}

		if (group_info->ngroups > 0)
			filter_arg.gid_len = group_info->ngroups + 1;
		else
			filter_arg.gid_len = 1;

		put_group_info(group_info);
	} else {
		filter_arg.dest_node = dest_node;
	}

	/* Run bpf filter program if it is already attached */
	if (bpf_filter) {
		/**
		 * Allocate dummy skb to pass required arguments to bpf
		 * filter program
		 */
		skb_bpf = alloc_skb(BPF_DATA_SIZE, GFP_ATOMIC);
		if (skb_bpf) {
			/* copy filter argument to skb */
			memcpy(skb_put(skb_bpf, BPF_DATA_SIZE), &filter_arg,
			       BPF_DATA_SIZE);

			/* execute eBPF filter here */
			rcu_read_lock();
			filter = rcu_dereference(bpf_filter);
			if (filter) {
				u32 status;
				/**
				 * Deny/grant permission based on return
				 * value of the filter
				 */
				status = bpf_prog_run_save_cb(filter->prog,
							      skb_bpf);
				err = status ? 0 : -EPERM;
			}
			rcu_read_unlock();
			kfree_skb(skb_bpf);
			if (err) {
				if (pkt_type == QRTR_TYPE_DATA)
					pr_err("qrtr: %s permission denied for client '%s' to SVC<0x%x:0x%x>\n",
					       __func__, current->comm,
					       service_id, instance_id);
				else
					pr_err("qrtr: %s SVC<0x%x:0x%x> broadcast denied to node %d\n",
					       __func__, service_id,
					       instance_id, dest_node);
			}
		}
	}

	return err;
}
EXPORT_SYMBOL(qrtr_run_bpf_filter);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. QRTR filter driver");
MODULE_LICENSE("GPL v2");
