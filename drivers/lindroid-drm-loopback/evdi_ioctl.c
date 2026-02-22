// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat
 * Copyright (c) 2015 - 2020 DisplayLink (UK) Ltd.
 * Copyright (c) 2025 Lindroid Authors
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include "evdi_drv.h"
#include "uapi/evdi_drm.h"
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/prefetch.h>
#include <linux/completion.h>
#include <linux/compat.h>
#include <linux/sched/signal.h>
#include <linux/errno.h>

static int evdi_queue_create_event_with_id(struct evdi_device *evdi, struct drm_evdi_gbm_create_buff *params, struct drm_file *owner, int poll_id);
int evdi_queue_destroy_event(struct evdi_device *evdi, int id, struct drm_file *owner);

struct evdi_gralloc_buf_stack {
	struct evdi_gralloc_buf_user buf;
	int installed_fds[EVDI_MAX_FDS];
};

static inline int evdi_get_unused_fds_batch(int n, int flags, int *fds)
{
	int i, fd, ret = 0;

	prefetchw(fds);

	if (unlikely(!fds || n <= 0))
		return ret;

	for (i = 0; i < n; i++)
		fds[i] = -1;

	for (i = 0; i < n; i++) {
		if (i + 1 < n)
			prefetchw(&fds[i+1]);
		fd = get_unused_fd_flags(flags);
		if (unlikely(fd < 0)) {
			ret = fd;
			break;
		}
		fds[i] = fd;
	}

	return ret;
}

static int evdi_process_gralloc_buffer(struct evdi_inflight_req *req,
					int *installed_fds,
					struct evdi_gralloc_buf_user *gralloc_buf)
{
	struct evdi_gralloc_data *gralloc;
	int i, fd_tmp;

	gralloc = req->reply.get_buf.gralloc_buf.gralloc;
	if (!gralloc)
		return -EINVAL;

	if (unlikely(gralloc->numFds < 0 || gralloc->numFds > EVDI_MAX_FDS ||
		     gralloc->numInts < 0 || gralloc->numInts > EVDI_MAX_INTS))
		return -EINVAL;

	gralloc_buf->version = gralloc->version;
	gralloc_buf->numFds = gralloc->numFds;
	gralloc_buf->numInts = gralloc->numInts;
	if (gralloc_buf->numInts) {
		memcpy(&gralloc_buf->data[gralloc_buf->numFds],
		       gralloc->data_ints,
		       sizeof(int) * gralloc_buf->numInts);
	}

	fd_tmp = evdi_get_unused_fds_batch(gralloc_buf->numFds, O_RDWR, installed_fds);
	if (unlikely(fd_tmp < 0)) {
		for (i = 0; i < gralloc_buf->numFds; i++) {
			if (installed_fds[i] >= 0)
				put_unused_fd(installed_fds[i]);
		}
		return fd_tmp;
	}
	for (i = 0; i < gralloc_buf->numFds; i++) {
		prefetchw(&gralloc_buf->data[i]);
		gralloc_buf->data[i] = installed_fds[i];
	}

	return 0;
}

//Allow partial progress; return -EFAULT only if zero progress
static int evdi_copy_from_user_allow_partial(void *dst, const void __user *src, size_t len)
{
	size_t not;

	if (!len)
		return 0;

	prefetchw(dst);
	not = copy_from_user(dst, src, len);
	if (not == len)
		return -EFAULT;

	return 0;
}

static int evdi_copy_to_user_allow_partial(void __user *dst, const void *src, size_t len)
{
	size_t not;

	if (!len)
		return 0;

	not = copy_to_user(dst, src, len);
	if (not == len)
		return -EFAULT;

	return 0;
}

static inline struct evdi_inflight_req *evdi_inflight_alloc(struct evdi_device *evdi,
						     struct drm_file *owner,
						     int type,
						     int *out_id)
{
	struct evdi_inflight_req *req;
	struct evdi_percpu_inflight *percpu_req;
	bool from_percpu = false;
	int id, i;

	percpu_req = get_cpu_ptr(evdi->percpu_inflight);
	if (likely(percpu_req)) {
		prefetchw(&percpu_req->req[0]);
		prefetchw(&percpu_req->req[1]);
		for (i = 0; i < 2; i++) {
			if (atomic_cmpxchg(&percpu_req->in_use[i], 0, 1) == 0) {
				req = &percpu_req->req[i];
				from_percpu = true;
				memset(req, 0, sizeof(*req));
				kref_init(&req->refcount);
				init_completion(&req->done);
				atomic_set(&req->freed, 0);
				atomic_set(&req->from_percpu, 1);
				req->percpu_slot = (u8)i;
				req->reply.get_buf.gralloc_buf.gralloc = NULL;
				EVDI_PERF_INC64(&evdi_perf.inflight_percpu_hits);
				break;
			}
		}
	}
	put_cpu_ptr(percpu_req);

	// fallback to mempool
	if (!from_percpu) {
		req = evdi_inflight_req_alloc(evdi);
		if (likely(req))
			EVDI_PERF_INC64(&evdi_perf.inflight_percpu_misses);
	}

	if (unlikely(!req))
		return NULL;

	req->type = type;
	req->owner = owner;

#ifdef EVDI_HAVE_XARRAY
	{
		u32 xid;
#ifndef EVDI_HAVE_XA_ALLOC_CYCLIC
		u32 start_id;
#endif
		int ret;
#ifdef EVDI_HAVE_XA_ALLOC_CYCLIC
		xid = READ_ONCE(evdi->inflight_next_id);
		if (unlikely(!xid))
			xid = 1;

		ret = xa_alloc_cyclic(&evdi->inflight_xa,
				      &xid, req,
				      XA_LIMIT(1, INT_MAX),
				      &evdi->inflight_next_id,
				      GFP_NOWAIT);
		if (ret == -EBUSY || ret == -ENOMEM || ret == -EEXIST) {
			WRITE_ONCE(evdi->inflight_next_id, 1);
			xid = 1;
			ret = xa_alloc_cyclic(&evdi->inflight_xa,
					      &xid, req,
					      XA_LIMIT(1, INT_MAX),
					      &evdi->inflight_next_id,
					      GFP_NOWAIT);
		}
		if (ret) {
			evdi_inflight_req_put(req);
			return NULL;
		}
		evdi_inflight_req_get(req);
		id = (int)xid;
#else
		xid = 0;
		start_id = READ_ONCE(evdi->inflight_next_id);
		if (unlikely(!start_id))
			start_id = 1;
		ret = xa_alloc(&evdi->inflight_xa, &xid, req,
			       XA_LIMIT(start_id, INT_MAX), GFP_NOWAIT);
		if (ret == -EBUSY && start_id > 1) {
			ret = xa_alloc(&evdi->inflight_xa, &xid, req,
				       XA_LIMIT(1, EVDI_MAX_INFLIGHT_REQUESTS), GFP_NOWAIT);
		}
		if (ret) {
			evdi_inflight_req_put(req);
			return NULL;
		}
		evdi_inflight_req_get(req);
		id = (int)xid;
#endif
	}
#else
	spin_lock(&evdi->inflight_lock);
	id = idr_alloc(&evdi->inflight_idr, req, 1, EVDI_MAX_INFLIGHT_REQUESTS, GFP_ATOMIC);
	spin_unlock(&evdi->inflight_lock);
	if (id < 0) {
		evdi_inflight_req_put(req);
		return NULL;
	}
	evdi_inflight_req_get(req);
#endif
	*out_id = id;
	return req;
}

static struct evdi_inflight_req *evdi_inflight_take(struct evdi_device *evdi, int id)
{
	struct evdi_inflight_req *req = NULL;
	if (unlikely(!evdi))
		return NULL;

#ifdef EVDI_HAVE_XARRAY
#ifdef EVDI_HAVE_ATOMIC_CMPXCHG_RELAXED
	req = xa_load(&evdi->inflight_xa, id);
	if (req) {
		if (xa_cmpxchg(&evdi->inflight_xa, id, req, NULL, GFP_NOWAIT) != req)
			req = NULL;
	}
#else
	{
		unsigned long flags;
		xa_lock_irqsave(&evdi->inflight_xa, flags);
		req = xa_load(&evdi->inflight_xa, id);
		if (req)
			xa_erase(&evdi->inflight_xa, id);

		xa_unlock_irqrestore(&evdi->inflight_xa, flags);
	}
#endif
#else
	spin_lock(&evdi->inflight_lock);
	req = idr_find(&evdi->inflight_idr, id);
	if (req)
		idr_remove(&evdi->inflight_idr, id);

	spin_unlock(&evdi->inflight_lock);
#endif
	return req;
}

void evdi_inflight_discard_owner(struct evdi_device *evdi, struct drm_file *owner)
{
	struct evdi_inflight_req *req;

	if (unlikely(!evdi || !owner))
		return;

#ifdef EVDI_HAVE_XARRAY
	{
		XA_STATE(xas, &evdi->inflight_xa, 0);

		rcu_read_lock();
		xas_for_each(&xas, req, ULONG_MAX) {
			if (req->owner != owner)
				continue;

#ifdef EVDI_HAVE_ATOMIC_CMPXCHG_RELAXED
			if (xa_cmpxchg(&evdi->inflight_xa,
				       xas.xa_index, req, NULL, GFP_NOWAIT) != req)
				continue;
#else
			if (xa_lock_irqsave(&evdi->inflight_xa, flags),
			    xa_for_each(&evdi->inflight_xa, idx, entry),
			    req == entry)
			{
				req = xa_erase(&evdi->inflight_xa, xas.xa_index);
				xa_unlock_irqrestore(&evdi->inflight_xa, flags);
			} else {
				xa_unlock_irqrestore(&evdi->inflight_xa, flags);
				continue;
			}
#endif
			rcu_read_unlock();
			complete_all(&req->done);
			evdi_inflight_req_put(req);
			cond_resched();
			rcu_read_lock();
		}
		rcu_read_unlock();
	}
#else
	{
		struct evdi_inflight_req *batch[16];
		int nr, i, id;

		do {
			nr = 0;
			id = 0;
			spin_lock(&evdi->inflight_lock);
			while (nr < 64) {
				req = idr_get_next(&evdi->inflight_idr, &id);
				if (!req)
					break;
				if (req->owner == owner) {
					idr_remove(&evdi->inflight_idr, id);
					batch[nr++] = req;
				}
				id++;
			}
			spin_unlock(&evdi->inflight_lock);

			for (i = 0; i < nr; i++) {
				complete_all(&batch[i]->done);
				evdi_inflight_req_put(batch[i]);
				cond_resched();
			}
		} while (nr == 16);
	}
#endif
}

static inline size_t evdi_event_serialize_payload(struct evdi_event *e,
						  void *out_buf,
						  size_t out_buf_size)
{
	size_t copy_size;

	if (unlikely(!e || !out_buf || !out_buf_size))
		return 0;

	copy_size = min_t(size_t, e->payload_size, out_buf_size);
	if (copy_size)
		memcpy(out_buf, e->payload, copy_size);

	return copy_size;
}

static int evdi_queue_create_event_with_id(struct evdi_device *evdi,
	   struct drm_evdi_gbm_create_buff *params,
	   struct drm_file *owner,
	   int poll_id)
{
	struct evdi_event *event = evdi_event_alloc(evdi, create_buf, poll_id,
		(void *)params, sizeof(*params), owner);
	if (!event)
		return -ENOMEM;

	evdi_event_queue(evdi, event);
	return 0;
}

static int evdi_queue_get_buf_event_with_id(struct evdi_device *evdi,
	struct drm_evdi_gbm_get_buff *params,
	struct drm_file *owner,
	int poll_id)
{
	struct evdi_event *event;

	event = evdi_event_alloc(evdi, get_buf, poll_id,
		(void *)params, sizeof(*params), owner);
	if (!event)
		return -ENOMEM;

	evdi_event_queue(evdi, event);
	return 0;
}

static inline void evdi_flush_work(struct evdi_device *evdi)
{
	if (unlikely(!evdi))
		return;

	atomic_set(&evdi->events.stopping, 1);
	evdi_smp_wmb();
	wake_up_interruptible(&evdi->events.wait_queue);
}

int evdi_ioctl_connect(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;
	struct drm_evdi_connect *cmd = data;

	EVDI_PERF_INC64(&evdi_perf.ioctl_calls[0]);

	if (!cmd->connected) {
		if (cmd->display_id >= LINDROID_MAX_CONNECTORS)
			return -EINVAL;
		evdi_flush_work(evdi);
		mutex_lock(&evdi->config_mutex);
		evdi->displays[cmd->display_id].connected = false;
		mutex_unlock(&evdi->config_mutex);
		{
			int i, any = 0;
			for (i = 0; i < LINDROID_MAX_CONNECTORS; i++)
				any |= evdi->displays[i].connected;
			if (!any)
				WRITE_ONCE(evdi->drm_client, NULL);
		}
		evdi_smp_wmb();

		evdi_info("Device %d disconnected", evdi->dev_index);
#ifdef EVDI_HAVE_KMS_HELPER
		drm_kms_helper_hotplug_event(dev);
#else
		drm_helper_hpd_irq_event(dev);
#endif
		return 0;
	}

	if (evdi->drm_client && evdi->drm_client != file) {
		evdi_warn("Device %d forcefully disconnecting previous client", evdi->dev_index);
		atomic_set(&evdi->events.stopping, 1);
		evdi_smp_wmb();
		wake_up_interruptible(&evdi->events.wait_queue);
	}

	if (cmd->display_id >= LINDROID_MAX_CONNECTORS)
		return -EINVAL;

	mutex_lock(&evdi->config_mutex);
	evdi->displays[cmd->display_id].connected = true;
	evdi->displays[cmd->display_id].width = cmd->width;
	evdi->displays[cmd->display_id].height = cmd->height;
	evdi->displays[cmd->display_id].refresh_rate = cmd->refresh_rate;
	mutex_unlock(&evdi->config_mutex);

	evdi_smp_wmb();
	WRITE_ONCE(evdi->drm_client, file);

	evdi_info("Device %d connected: %ux%u@%uHz id:%u",
		  evdi->dev_index, cmd->width, cmd->height, cmd->refresh_rate, cmd->display_id);

	atomic_set(&evdi->events.stopping, 0);

#ifdef EVDI_HAVE_KMS_HELPER
	drm_kms_helper_hotplug_event(dev);
#else
	drm_helper_hpd_irq_event(dev);
#endif
	return 0;
}

static __always_inline bool evdi_swap_mailbox_read_stable(struct evdi_device *evdi,
							 int display_id,
							 u64 *seq,
							 u64 *payload,
							 int *poll_id,
							 struct drm_file **owner)
{
	struct evdi_swap_mailbox *mb;
	u64 s1, s2, p;
	int pid;
	struct drm_file *o;
	int tries = 0;

	if (unlikely(!evdi))
		return false;

	if (unlikely(display_id < 0 || display_id >= LINDROID_MAX_CONNECTORS))
		return false;

	mb = &evdi->swap_mailbox[display_id];

	for (;;) {
		s1 = (u64)atomic64_read(&mb->seq);
		if (s1 & 1)
			goto retry;

		evdi_smp_rmb();
		p = (u64)atomic64_read(&mb->payload);
		pid = atomic_read(&mb->poll_id);
		o = READ_ONCE(mb->owner);
		evdi_smp_rmb();

		s2 = (u64)atomic64_read(&mb->seq);
		if (likely(s1 == s2 && !(s2 & 1)))
			break;

retry:
		if (++tries >= 8)
			return false;
		cpu_relax();
	}

	*seq = s2;
	*payload = p;
	*poll_id = pid;
	*owner = o;
	return true;
}

static __always_inline bool evdi_swap_dequeue_for_file(struct evdi_device *evdi,
						       struct drm_file *file,
						       struct evdi_swap *out,
						       int *out_poll_id)
{
	struct evdi_file_priv *priv;
	int start, i;

	if (unlikely(!evdi || !file || !out || !out_poll_id))
		return false;

	priv = file->driver_priv;
	if (unlikely(!priv))
		return false;

	start = (int)(priv->swap_rr % LINDROID_MAX_CONNECTORS);
	for (i = 0; i < LINDROID_MAX_CONNECTORS; i++) {
		const int d = (start + i) % LINDROID_MAX_CONNECTORS;
		u64 seq, payload;
		int poll_id;
		struct drm_file *owner;

		if (!evdi_swap_mailbox_read_stable(evdi, d, &seq, &payload, &poll_id, &owner))
			continue;

		if (owner != file)
			continue;
		if (seq == priv->last_swap_seq[d])
			continue;

		priv->last_swap_seq[d] = seq;
		priv->swap_rr = (u8)((d + 1) % LINDROID_MAX_CONNECTORS);

		out->id = (int)(u32)(payload >> 32);
		out->display_id = (int)(u32)payload;
		*out_poll_id = poll_id;
		return true;
	}

	return false;
}

int evdi_ioctl_poll(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;
	struct drm_evdi_poll *cmd = data;
	struct evdi_event *event;
	struct evdi_swap sw;
	size_t payload_size;
	int ret, poll_id;

	u8 payload_buf[EVDI_EVENT_PAYLOAD_MAX];

	EVDI_PERF_INC64(&evdi_perf.ioctl_calls[1]);

	/* swap mailbox fast path */
	if (evdi_swap_dequeue_for_file(evdi, file, &sw, &poll_id)) {
		cmd->event = swap_to;
		cmd->poll_id = poll_id;
		if (cmd->data) {
			if (evdi_copy_to_user_allow_partial(cmd->data, &sw, sizeof(sw)))
				return -EFAULT;
		}
		EVDI_PERF_INC64(&evdi_perf.swap_delivered);
		return 0;
	}

	event = evdi_event_dequeue(evdi);
	if (likely(event)) {
		cmd->event = event->type;
		cmd->poll_id = event->poll_id;
		payload_size = evdi_event_serialize_payload(event,
			payload_buf, sizeof(payload_buf));
		if (payload_size && cmd->data) {
			if (evdi_copy_to_user_allow_partial(cmd->data,
				payload_buf, payload_size)) {
				evdi_event_free(event);
				return -EFAULT;
			}
		}
		evdi_event_free(event);
		return 0;
	}

	ret = evdi_event_wait(evdi, file);
	if (ret)
		return ret;

	if (evdi_swap_dequeue_for_file(evdi, file, &sw, &poll_id)) {
		cmd->event = swap_to;
		cmd->poll_id = poll_id;
		if (cmd->data) {
			if (evdi_copy_to_user_allow_partial(cmd->data, &sw, sizeof(sw)))
				return -EFAULT;
		}
		EVDI_PERF_INC64(&evdi_perf.swap_delivered);
		return 0;
	}

	event = evdi_event_dequeue(evdi);
	if (!event)
		return -EAGAIN;

	cmd->event = event->type;
	cmd->poll_id = event->poll_id;

	payload_size = evdi_event_serialize_payload(event, payload_buf,
		sizeof(payload_buf));
	if (payload_size && cmd->data) {
		if (evdi_copy_to_user_allow_partial(cmd->data,
			payload_buf, payload_size)) {
			evdi_event_free(event);
			return -EFAULT;
		}
	}

	evdi_event_free(event);
	return 0;
}

int evdi_ioctl_gbm_get_buff(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;
	struct drm_evdi_gbm_get_buff *cmd = data;
	struct evdi_inflight_req *req;
	struct drm_evdi_gbm_get_buff evt_params;
	struct evdi_gralloc_buf_stack stack_buf;
	struct evdi_gralloc_buf_user *gralloc_buf;
	struct evdi_gralloc_data *gralloc;
	int poll_id;
	long ret;
	int i, nfd, copy_size;

	EVDI_PERF_INC64(&evdi_perf.ioctl_calls[7]);

	req = evdi_inflight_alloc(evdi, file, get_buf, &poll_id);
	if (!req)
		return -ENOMEM;

	memset(&evt_params, 0, sizeof(evt_params));
	evt_params.id = cmd->id;
	evt_params.native_handle = NULL;

	if (evdi_queue_get_buf_event_with_id(evdi, &evt_params, file, poll_id)) {
		struct evdi_inflight_req *tmp = evdi_inflight_take(evdi, poll_id);
		if (tmp)
			evdi_inflight_req_put(tmp);

		evdi_inflight_req_put(req);
		return -ENOMEM;
	}

	ret = wait_for_completion_interruptible_timeout(&req->done, EVDI_WAIT_TIMEOUT);
	if (ret == 0) {
			evdi_inflight_req_put(req);
			return -ETIMEDOUT;
	}
	if (ret < 0) {
			evdi_inflight_req_put(req);
			return (int)ret;
	}

	gralloc_buf = &stack_buf.buf;

	ret = evdi_process_gralloc_buffer(req, stack_buf.installed_fds, gralloc_buf);
	if (ret) {
		evdi_inflight_req_put(req);
		return ret;
	}

	gralloc = req->reply.get_buf.gralloc_buf.gralloc;
	copy_size = sizeof(int) * (3 + gralloc_buf->numFds + gralloc_buf->numInts);
	if (gralloc)
		prefetch(gralloc);

	if (evdi_copy_to_user_allow_partial(cmd->native_handle, gralloc_buf, copy_size)) {
		for (i = 0; i < gralloc_buf->numFds; i++)
			put_unused_fd(stack_buf.installed_fds[i]);

		ret = -EFAULT;
		goto err_event;
	}

	if (gralloc) {
		nfd = gralloc_buf->numFds;
		if (nfd < 0)
			nfd = 0;
		else if (nfd > EVDI_MAX_FDS)
			nfd = EVDI_MAX_FDS;

		for (i = 0; i < nfd; i++) {
			if (gralloc->data_files[i])
				fd_install(stack_buf.installed_fds[i], gralloc->data_files[i]);
		}
	}

	ret = 0;
err_event:
	if (gralloc) {
		nfd = gralloc->numFds;

		if (nfd < 0)
			nfd = 0;
		else if (nfd > EVDI_MAX_FDS)
			nfd = EVDI_MAX_FDS;

		if (ret) {
			for (i = 0; i < nfd; i++) {
				if (gralloc->data_files[i]) {
					fput(gralloc->data_files[i]);
					gralloc->data_files[i] = NULL;
				}
			}
		} else {
			for (i = 0; i < nfd; i++) {
				gralloc->data_files[i] = NULL;
			}
		}
	}
	evdi_inflight_req_put(req);
	return ret;
}

static inline void evdi_file_track_buffer(struct drm_file *file, int id)
{
	struct evdi_file_priv *priv;
	int ret = 0;

	if (unlikely(!file || id <= 0))
		return;

	priv = file->driver_priv;
	if (unlikely(!priv))
		return;

	mutex_lock(&priv->lock);

#ifdef EVDI_HAVE_XARRAY
#ifdef EVDI_HAVE_XA_ALLOC_CYCLIC
	{
		void *entry;
		u32 handle = 0;

		entry = xa_load(&priv->bufid_to_handle, id);
		if (entry)
			goto out_unlock;

		ret = xa_alloc_cyclic(&priv->handle_to_bufid, &handle,
				      xa_mk_value((unsigned long)id),
				      XA_LIMIT(1, INT_MAX),
				      &priv->next_handle, GFP_KERNEL);
		if (ret)
			goto out_unlock;

		ret = xa_err(xa_store(&priv->bufid_to_handle, id,
				      xa_mk_value((unsigned long)handle),
				      GFP_KERNEL));
		if (ret) {
			xa_erase(&priv->handle_to_bufid, (unsigned long)handle);
			goto out_unlock;
		}
	}
#else
	ret = xa_err(xa_store(&priv->buffers, id, xa_mk_value(1), GFP_KERNEL));
	goto out_unlock;
#endif
#else
	ret = idr_alloc(&priv->buffers, (void *)1, id, id + 1, GFP_KERNEL);
	if (ret == id)
		ret = 0;
	goto out_unlock;
#endif

out_unlock:
	mutex_unlock(&priv->lock);

	if (unlikely(ret))
		evdi_warn("Failed to track buffer %d (%d)", id, ret);
}

static inline void evdi_file_untrack_buffer(struct drm_file *file, int id)
{
	struct evdi_file_priv *priv;

	if (unlikely(!file || id <= 0))
		return;

	priv = file->driver_priv;
	if (unlikely(!priv))
		return;

	mutex_lock(&priv->lock);

#ifdef EVDI_HAVE_XARRAY
#ifdef EVDI_HAVE_XA_ALLOC_CYCLIC
	{
		void *entry;
		u32 handle;

		entry = xa_load(&priv->bufid_to_handle, id);
		if (!entry)
			goto out_unlock;

		handle = (u32)xa_to_value(entry);
		xa_erase(&priv->bufid_to_handle, id);
		xa_erase(&priv->handle_to_bufid, (unsigned long)handle);
	}
#else
	xa_erase(&priv->buffers, id);
	goto out_unlock;
#endif
#else
	idr_remove(&priv->buffers, id);
	goto out_unlock;
#endif

out_unlock:
	mutex_unlock(&priv->lock);
}

int evdi_ioctl_gbm_create_buff(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;
	struct drm_evdi_gbm_create_buff *cmd = data;
	struct evdi_inflight_req *req;
	struct evdi_inflight_req *tmp;
	struct drm_evdi_gbm_create_buff evt_params;
	int __user *u_id;
	__u32 __user *u_stride;
	int poll_id;
	long wret;

	u_id = cmd->id;
	u_stride = cmd->stride;
	if (u_id && !evdi_access_ok_write(u_id, sizeof(*u_id)))
		return -EFAULT;

	if (u_stride && !evdi_access_ok_write(u_stride, sizeof(*u_stride)))
		return -EFAULT;

	req = evdi_inflight_alloc(evdi, file, create_buf, &poll_id);
	if (!req)
		return -ENOMEM;

	memset(&evt_params, 0, sizeof(evt_params));
	evt_params.format = cmd->format;
	evt_params.width = cmd->width;
	evt_params.height = cmd->height;
	evt_params.id = NULL;
	evt_params.stride = NULL;

	if (evdi_queue_create_event_with_id(evdi, &evt_params, file, poll_id)) {
		tmp = evdi_inflight_take(evdi, poll_id);
		if (tmp)
			evdi_inflight_req_put(tmp);

		evdi_inflight_req_put(req);

		return -ENOMEM;
	}

	wret = wait_for_completion_interruptible_timeout(&req->done, EVDI_WAIT_TIMEOUT);
	if (wret == 0) {
		evdi_inflight_req_put(req);
		return -ETIMEDOUT;
	}
	if (wret < 0) {
		evdi_inflight_req_put(req);
		return (int)wret;
	}

	if (u_id) {
		if (evdi_copy_to_user_allow_partial(u_id, &req->reply.create.id, sizeof(*u_id))) {
			evdi_inflight_req_put(req);
			return -EFAULT;
		}
	}

	evdi_file_track_buffer(file, req->reply.create.id);

	if (u_stride) {
		if (evdi_copy_to_user_allow_partial(u_stride, &req->reply.create.stride, sizeof(*u_stride))) {
			evdi_inflight_req_put(req);
			return -EFAULT;
		}
	}

	evdi_inflight_req_put(req);
	return 0;
}

int evdi_ioctl_get_buff_callback(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;
	struct drm_evdi_get_buff_callabck *cb = data;
	struct evdi_inflight_req *req;
	struct evdi_gralloc_data *gralloc;
	int i, j, nfd, nint;
	int fds_local[EVDI_MAX_FDS];

	EVDI_PERF_INC64(&evdi_perf.ioctl_calls[3]);

	req = evdi_inflight_take(evdi, cb->poll_id);
	if (!req)
		goto out_wake;

//	if (req->owner != file)
//		evdi_warn("get_buff_callback: poll_id %d owned by different file", cb->poll_id);

	if (cb->numFds < 0 || cb->numInts < 0 ||
	    cb->numFds > EVDI_MAX_FDS || cb->numInts > EVDI_MAX_INTS)
		goto out_complete;

	nfd = cb->numFds;
	nint = cb->numInts;

	gralloc = mempool_alloc(global_event_pool.gralloc_data_pool, GFP_KERNEL);
	if (!gralloc)
		goto out_complete;

	memset(gralloc, 0, sizeof(*gralloc));

	gralloc->version = cb->version;
	gralloc->numFds = 0;
	gralloc->numInts = 0;

	if (nint) {
		if (evdi_copy_from_user_allow_partial(gralloc->data_ints,
						      cb->data_ints,
						      sizeof(int) * nint)) {
			mempool_free(gralloc, global_event_pool.gralloc_data_pool);
			goto out_complete;
		}
		gralloc->numInts = nint;
	}

	if (nfd) {
		if (evdi_copy_from_user_allow_partial(fds_local, cb->fd_ints,
						      sizeof(int) * nfd)) {
			mempool_free(gralloc, global_event_pool.gralloc_data_pool);
			goto out_complete;
		}
		for (i = 0; i < nfd; i++) {
			gralloc->data_files[i] = fget(fds_local[i]);
			if (!gralloc->data_files[i]) {
				for (j = 0; j < i; j++) {
					if (gralloc->data_files[j]) {
						fput(gralloc->data_files[j]);
						gralloc->data_files[j] = NULL;
					}
				}
				mempool_free(gralloc, global_event_pool.gralloc_data_pool);
				goto out_complete;
			}
		}
		gralloc->numFds = nfd;
	}

	req->reply.get_buf.gralloc_buf.gralloc = gralloc;

out_complete:
	complete_all(&req->done);
	evdi_inflight_req_put(req);
	goto out_wake;

out_wake:
	evdi_wakeup_pollers(evdi);
	return 0;
}

int evdi_ioctl_destroy_buff_callback(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;

	EVDI_PERF_INC64(&evdi_perf.ioctl_calls[4]);

	evdi_wakeup_pollers(evdi);

	return 0;
}

int evdi_ioctl_swap_callback(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;

	struct drm_evdi_swap_callback *cb = data;
	int d;

	if (unlikely(!evdi || !cb))
		return -EINVAL;

	for (d = 0; d < LINDROID_MAX_CONNECTORS; d++) {
		if (!atomic_read(&evdi->swap_pending[d]))
			continue;
		if (atomic_read(&evdi->swap_pending_pollid[d]) != cb->poll_id)
			continue;

		atomic_set(&evdi->swap_pending_pollid[d], 0);
		atomic_set(&evdi->swap_pending[d], 0);
		wake_up_interruptible(&evdi->swap_ack_waitq);
		break;
	}

	EVDI_PERF_INC64(&evdi_perf.ioctl_calls[5]);

	evdi_wakeup_pollers(evdi);

	return 0;
}

int evdi_ioctl_create_buff_callback(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;
	struct drm_evdi_create_buff_callabck *cb = data;
	struct evdi_inflight_req *req;

	EVDI_PERF_INC64(&evdi_perf.ioctl_calls[6]);

	req = evdi_inflight_take(evdi, cb->poll_id);
	if (req) {
		if (cb->id < 0 || cb->stride < 0) {
			req->reply.create.id = 0;
			req->reply.create.stride = 0;
		} else {
			req->reply.create.id = cb->id;
			req->reply.create.stride = cb->stride;
		}
		complete_all(&req->done);
		evdi_inflight_req_put(req);
	} else {
		evdi_warn("create_buff_callback: poll_id %d not found", cb->poll_id);
	}

	return 0;
}

int evdi_ioctl_gbm_del_buff(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct evdi_device *evdi = dev->dev_private;
	struct drm_evdi_gbm_del_buff *cmd = data;
	long ret;

	ret = evdi_queue_destroy_event(evdi, cmd->id, file);
	if (!ret)
		evdi_file_untrack_buffer(file, cmd->id);

	return ret;
}

static int evdi_queue_int_event(struct evdi_device *evdi,
	enum poll_event_type type, int v, struct drm_file *owner)
{
	struct evdi_event *event;

	event = evdi_event_alloc(evdi, type,
		atomic_inc_return(&evdi->events.next_poll_id),
		(void *)&v, sizeof(int), owner);

	if (!event)
		return -ENOMEM;

	evdi_event_queue(evdi, event);
	return 0;
}

int evdi_queue_swap_event(struct evdi_device *evdi,
	int id, int display_id, struct drm_file *owner)
{
	struct evdi_swap_mailbox *mb;
	struct drm_file *client;
	u64 payload;
	int poll_id;

	if (unlikely(!evdi))
		return -EINVAL;
	if (unlikely(display_id < 0 || display_id >= LINDROID_MAX_CONNECTORS))
		return -EINVAL;
	if (unlikely(atomic_read(&evdi->events.stopping)))
		return -ENODEV;

	/* Do not overwrite an un-ACKed swap */
	if (atomic_cmpxchg(&evdi->swap_pending[display_id], 0, 1) != 0)
		return -EBUSY;

	client = READ_ONCE(evdi->drm_client);
	if (client)
		owner = client;

	if (unlikely(!owner)) {
		atomic_set(&evdi->swap_pending[display_id], 0);
		return -ENODEV;
	}

	mb = &evdi->swap_mailbox[display_id];
	payload = evdi_swap_pack(id, display_id);
	poll_id = atomic_inc_return(&evdi->events.next_poll_id);

	atomic_set(&evdi->swap_pending_pollid[display_id], poll_id);

	atomic64_inc(&mb->seq); // odd
	WRITE_ONCE(mb->owner, owner);
	atomic_set(&mb->poll_id, poll_id);
	atomic64_set(&mb->payload, payload);
	evdi_smp_wmb();
	atomic64_inc(&mb->seq); // even

	EVDI_PERF_INC64(&evdi_perf.swap_updates);
	wake_up_interruptible(&evdi->events.wait_queue);
	return 0;
}

int evdi_queue_add_buf_event(struct evdi_device *evdi, int fd_data, struct drm_file *owner)
{
	return evdi_queue_int_event(evdi, add_buf, fd_data, owner);
}

int evdi_queue_get_buf_event(struct evdi_device *evdi, int id, struct drm_file *owner)
{
	return evdi_queue_int_event(evdi, get_buf, id, owner);
}

int evdi_queue_destroy_event(struct evdi_device *evdi, int id, struct drm_file *owner)
{
	return evdi_queue_int_event(evdi, destroy_buf, id, owner);
}

int evdi_queue_create_event(struct evdi_device *evdi,
			   struct drm_evdi_gbm_create_buff *params,
			   struct drm_file *owner)
{
	int poll_id = atomic_inc_return(&evdi->events.next_poll_id);
	return evdi_queue_create_event_with_id(evdi, params, owner, poll_id);
}
