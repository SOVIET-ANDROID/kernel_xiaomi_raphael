/*
 * Copyright (c) 2024 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: wlan_hdd_frame_inject.c
 *
 * WLAN Host Device Driver Frame Injection Implementation
 */

#include "wlan_hdd_includes.h"
#include "wlan_hdd_frame_inject.h"
#include "wlan_hdd_frame_validate.h"
#include "wlan_hdd_inject_security.h"
#include "wma_frame_inject.h"
#include <linux/if.h>
#include <linux/netdevice.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <qdf_mem.h>
#include <qdf_trace.h>
#include <qdf_nbuf.h>
#include "wlan_hdd_cfg80211.h"

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/* Logging macros for frame injection */
#define hdd_inject_debug(params...) \
	QDF_TRACE_DEBUG(QDF_MODULE_ID_HDD, params)
#define hdd_inject_info(params...) \
	QDF_TRACE_INFO(QDF_MODULE_ID_HDD, params)
#define hdd_inject_warn(params...) \
	QDF_TRACE_WARN(QDF_MODULE_ID_HDD, params)
#define hdd_inject_err(params...) \
	QDF_TRACE_ERROR(QDF_MODULE_ID_HDD, params)

/* Global session ID counter */
static qdf_atomic_t g_injection_session_id;

/**
 * hdd_init_injection_session_id() - Initialize session ID counter
 */
static void hdd_init_injection_session_id(void)
{
	qdf_atomic_init(&g_injection_session_id);
	qdf_atomic_set(&g_injection_session_id, 1);
}

/**
 * hdd_get_next_session_id() - Get next unique session ID
 *
 * Return: Unique session ID
 */
static uint32_t hdd_get_next_session_id(void)
{
	return qdf_atomic_inc_return(&g_injection_session_id);
}

/**
 * hdd_init_frame_injection() - Initialize frame injection for adapter
 * @adapter: HDD adapter
 *
 * This function initializes frame injection context for the given adapter.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_init_frame_injection(struct hdd_adapter *adapter)
{
	struct hdd_injection_ctx *injection_ctx;
	QDF_STATUS status;
	static bool session_id_initialized;

	hdd_inject_debug("Initializing frame injection for adapter %pK", adapter);

	if (!adapter) {
		hdd_inject_err("Adapter is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	/* Initialize global session ID counter on first use */
	if (!session_id_initialized) {
		hdd_init_injection_session_id();
		session_id_initialized = true;
	}

	/* Allocate injection context */
	injection_ctx = qdf_mem_malloc(sizeof(*injection_ctx));
	if (!injection_ctx) {
		hdd_inject_err("Failed to allocate injection context");
		return QDF_STATUS_E_NOMEM;
	}

	/* Initialize injection queue */
	qdf_list_create(&injection_ctx->injection_queue,
			HDD_FRAME_INJECT_MAX_QUEUE_SIZE);

	/* Initialize queue lock */
	qdf_spinlock_create(&injection_ctx->queue_lock);

	/* Initialize security context */
	status = hdd_init_injection_security_ctx(&injection_ctx->security_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_err("Failed to initialize security context: %d", status);
		qdf_spinlock_destroy(&injection_ctx->queue_lock);
		qdf_list_destroy(&injection_ctx->injection_queue);
		qdf_mem_free(injection_ctx);
		return status;
	}

	/* Initialize other fields */
	injection_ctx->is_monitor_mode = false;
	injection_ctx->adapter = adapter;
	injection_ctx->wma_handle = cds_get_context(QDF_MODULE_ID_WMA);
	if (!injection_ctx->wma_handle)
		hdd_inject_warn("WMA handle is not ready; frame injection TX may be unavailable");

	/* Initialize recovery context */
	qdf_mem_zero(&injection_ctx->recovery_ctx, sizeof(injection_ctx->recovery_ctx));
	injection_ctx->recovery_ctx.recovery_in_progress = false;
	injection_ctx->recovery_ctx.consecutive_errors = 0;
	injection_ctx->recovery_ctx.recovery_attempts = 0;

	/* Initialize recovery work and timer */
	qdf_create_work(0, &injection_ctx->recovery_ctx.recovery_work,
			hdd_injection_recovery_work, adapter);
	
	status = qdf_timer_init(NULL, &injection_ctx->recovery_ctx.recovery_timer,
				hdd_injection_recovery_timer, adapter, QDF_TIMER_TYPE_SW);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_err("Failed to initialize recovery timer: %d", status);
		qdf_destroy_work(NULL, &injection_ctx->recovery_ctx.recovery_work);
		hdd_deinit_injection_security_ctx(&injection_ctx->security_ctx);
		qdf_spinlock_destroy(&injection_ctx->queue_lock);
		qdf_list_destroy(&injection_ctx->injection_queue);
		qdf_mem_free(injection_ctx);
		return status;
	}

	/* Initialize work queue for processing injection requests */
	qdf_create_work(0, &injection_ctx->queue_work, 
			hdd_process_injection_queue_work, injection_ctx);

	/* Assign to adapter */
	adapter->injection_ctx = injection_ctx;

	/* Create debugfs entries for this adapter */
	status = hdd_injection_create_debugfs_entries(adapter);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_warn("Failed to create debugfs entries: %d", status);
		/* Don't fail initialization for debug interface failure */
	}

	hdd_inject_info("Frame injection initialized successfully for adapter %pK", adapter);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_deinit_frame_injection() - Cleanup frame injection for adapter
 * @adapter: HDD adapter
 *
 * This function cleans up frame injection context for the given adapter.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_deinit_frame_injection(struct hdd_adapter *adapter)
{
	struct hdd_injection_ctx *injection_ctx;
	struct inject_frame_req *req;
	qdf_list_node_t *node, *next_node;
	QDF_STATUS status;

	hdd_inject_debug("Cleaning up frame injection for adapter %pK", adapter);

	if (!adapter || !adapter->injection_ctx) {
		hdd_inject_err("Invalid adapter or injection context");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;

	/* Remove debugfs entries for this adapter */
	hdd_injection_remove_debugfs_entries(adapter);

	/* Cancel any pending work */
	qdf_cancel_work(&injection_ctx->queue_work);
	qdf_flush_work(&injection_ctx->queue_work);

	/* Cancel recovery work and timer */
	qdf_cancel_work(&injection_ctx->recovery_ctx.recovery_work);
	qdf_flush_work(&injection_ctx->recovery_ctx.recovery_work);
	qdf_timer_stop(&injection_ctx->recovery_ctx.recovery_timer);
	qdf_timer_free(&injection_ctx->recovery_ctx.recovery_timer);

	/* Clean up injection queue */
	qdf_spin_lock_bh(&injection_ctx->queue_lock);
	
	status = qdf_list_peek_front(&injection_ctx->injection_queue, &node);
	while (QDF_IS_STATUS_SUCCESS(status)) {
		req = qdf_container_of(node, struct inject_frame_req, node);
		
		status = qdf_list_peek_next(&injection_ctx->injection_queue, node, &next_node);
		
		qdf_list_remove_node(&injection_ctx->injection_queue, node);
		
		/* Free frame data */
		if (req->frame_data)
			qdf_mem_free(req->frame_data);
		qdf_mem_free(req);
		
		node = next_node;
	}
	
	qdf_spin_unlock_bh(&injection_ctx->queue_lock);

	/* Cleanup security context */
	hdd_deinit_injection_security_ctx(&injection_ctx->security_ctx);

	/* Destroy queue and lock */
	qdf_list_destroy(&injection_ctx->injection_queue);
	qdf_spinlock_destroy(&injection_ctx->queue_lock);

	/* Free injection context */
	qdf_mem_free(injection_ctx);
	adapter->injection_ctx = NULL;

	hdd_inject_info("Frame injection cleaned up successfully for adapter %pK", adapter);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_frame_inject_enable() - Enable frame injection for adapter
 * @adapter: HDD adapter
 *
 * This function enables frame injection capabilities for the adapter.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_frame_inject_enable(struct hdd_adapter *adapter)
{
	struct hdd_injection_ctx *injection_ctx;

	hdd_inject_debug("Enabling frame injection for adapter %pK", adapter);

	if (!adapter || !adapter->injection_ctx) {
		hdd_inject_err("Invalid adapter or injection context");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	injection_ctx->is_monitor_mode = true;

	hdd_inject_info("Frame injection enabled for adapter %pK", adapter);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_frame_inject_disable() - Disable frame injection for adapter
 * @adapter: HDD adapter
 *
 * This function disables frame injection capabilities for the adapter.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_frame_inject_disable(struct hdd_adapter *adapter)
{
	struct hdd_injection_ctx *injection_ctx;

	hdd_inject_debug("Disabling frame injection for adapter %pK", adapter);

	if (!adapter || !adapter->injection_ctx) {
		hdd_inject_err("Invalid adapter or injection context");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	injection_ctx->is_monitor_mode = false;

	hdd_inject_info("Frame injection disabled for adapter %pK", adapter);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_create_injection_request() - Create frame injection request
 * @ioctl_data: IOCTL data from userspace
 * @req: Output injection request
 *
 * This function creates a frame injection request from IOCTL data.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_create_injection_request(struct hdd_frame_inject_ioctl *ioctl_data,
					       struct inject_frame_req **req)
{
	struct inject_frame_req *injection_req;
	uint8_t *frame_data;

	hdd_inject_debug("Creating injection request: len=%u, flags=0x%x",
			 ioctl_data->frame_len, ioctl_data->tx_flags);

	if (!ioctl_data || !req) {
		hdd_inject_err("Invalid parameters");
		return QDF_STATUS_E_INVAL;
	}

	/* Validate frame length */
	if (ioctl_data->frame_len == 0 || 
	    ioctl_data->frame_len > HDD_FRAME_INJECT_MAX_SIZE) {
		hdd_inject_err("Invalid frame length: %u", ioctl_data->frame_len);
		return QDF_STATUS_E_INVAL;
	}

	/* Allocate injection request */
	injection_req = qdf_mem_malloc(sizeof(*injection_req));
	if (!injection_req) {
		hdd_inject_err("Failed to allocate injection request");
		return QDF_STATUS_E_NOMEM;
	}

	/* Allocate frame data buffer */
	frame_data = qdf_mem_malloc(ioctl_data->frame_len);
	if (!frame_data) {
		hdd_inject_err("Failed to allocate frame data buffer");
		qdf_mem_free(injection_req);
		return QDF_STATUS_E_NOMEM;
	}

	/* Copy frame data from userspace */
	if (copy_from_user(frame_data, ioctl_data->frame_data, ioctl_data->frame_len)) {
		hdd_inject_err("Failed to copy frame data from userspace");
		qdf_mem_free(frame_data);
		qdf_mem_free(injection_req);
		return QDF_STATUS_E_FAULT;
	}

	/* Initialize injection request */
	injection_req->frame_len = ioctl_data->frame_len;
	injection_req->frame_data = frame_data;
	injection_req->tx_flags = ioctl_data->tx_flags;
	injection_req->retry_count = ioctl_data->retry_count;
	/* Initialize timing fields for performance monitoring */
	injection_req->submit_time = qdf_get_log_timestamp();
	injection_req->queue_time = 0;
	injection_req->process_time = 0;
	injection_req->complete_time = 0;
	injection_req->tx_rate = ioctl_data->tx_rate;
	injection_req->timestamp = qdf_get_log_timestamp();
	injection_req->session_id = hdd_get_next_session_id();

	*req = injection_req;

	hdd_inject_debug("Injection request created successfully: session_id=%u",
			 injection_req->session_id);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_process_frame_injection() - Process frame injection request
 * @adapter: HDD adapter
 * @req: Frame injection request
 *
 * This function processes a frame injection request by validating
 * the frame and queuing it for transmission.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_process_frame_injection(struct hdd_adapter *adapter,
				       struct inject_frame_req *req)
{
	struct hdd_injection_ctx *injection_ctx;
	QDF_STATUS status;
	uint8_t frame_type;

	hdd_inject_debug("Processing frame injection: session_id=%u, len=%u",
			 req->session_id, req->frame_len);

	if (!adapter || !adapter->injection_ctx || !req) {
		hdd_inject_err("Invalid parameters");
		return QDF_STATUS_E_INVAL;
	}

	/* Check global enable flag */
	if (!hdd_injection_is_globally_enabled()) {
		hdd_inject_warn("Frame injection is globally disabled");
		return QDF_STATUS_E_PERM;
	}

	injection_ctx = adapter->injection_ctx;
	if (!injection_ctx->wma_handle)
		injection_ctx->wma_handle = cds_get_context(QDF_MODULE_ID_WMA);

	/* Validate permissions and apply rate limiting */
	status = hdd_validate_injection_permissions(adapter, req);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_warn("Permission validation failed: %d", status);
		return status;
	}

	/* Validate frame size limits */
	status = hdd_check_frame_size_limits(req->frame_len);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_update_injection_stats(adapter, HDD_INJECTION_STAT_VALIDATION_FAILURES, 1);
		hdd_inject_err("Frame size validation failed: %d", status);
		return status;
	}

	/* Validate 802.11 frame format */
	status = hdd_validate_80211_frame(req->frame_data, req->frame_len);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_update_injection_stats(adapter, HDD_INJECTION_STAT_VALIDATION_FAILURES, 1);
		hdd_inject_err("Frame format validation failed: %d", status);
		return status;
	}

	/* Sanitize frame content */
	status = hdd_sanitize_frame_content(req->frame_data, req->frame_len);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_update_injection_stats(adapter, HDD_INJECTION_STAT_VALIDATION_FAILURES, 1);
		hdd_inject_err("Frame sanitization failed: %d", status);
		return status;
	}

	/*
	 * Injection currently transmits via WMI mgmt-tx path, so only 802.11
	 * management frames are supported on this path.
	 */
	frame_type = req->frame_data[0] & 0x0c;
	if (frame_type != 0x00) {
		hdd_update_injection_stats(adapter, HDD_INJECTION_STAT_VALIDATION_FAILURES, 1);
		hdd_inject_warn("Dropping non-management injection frame: fc_type=0x%02x len=%u",
				frame_type, req->frame_len);
		return QDF_STATUS_E_NOSUPPORT;
	}

	/* Queue frame for injection */
	qdf_spin_lock_bh(&injection_ctx->queue_lock);
	
	/* Check queue size limit */
	if (qdf_list_size(&injection_ctx->injection_queue) >= 
	    injection_ctx->security_ctx.config.max_queue_size) {
		qdf_spin_unlock_bh(&injection_ctx->queue_lock);
		hdd_update_injection_stats(adapter, HDD_INJECTION_STAT_QUEUE_OVERFLOWS, 1);
		hdd_inject_warn("Injection queue is full");
		return QDF_STATUS_E_RESOURCES;
	}
	
	status = qdf_list_insert_back(&injection_ctx->injection_queue, &req->node);
	qdf_spin_unlock_bh(&injection_ctx->queue_lock);

	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_err("Failed to queue injection request: %d", status);
		return status;
	}

	/* Update timing for queue entry */
	req->queue_time = qdf_get_log_timestamp();

	/* Update statistics for successful submission */
	hdd_update_injection_stats(adapter, HDD_INJECTION_STAT_FRAMES_SUBMITTED, 1);

	/* Update throughput monitoring */
	hdd_update_injection_throughput(adapter);

	/* Monitor resource usage */
	hdd_monitor_injection_resources(adapter);

	/* Schedule work to process the queue */
	qdf_sched_work(0, &injection_ctx->queue_work);

	hdd_inject_debug("Frame injection queued successfully: session_id=%u",
			 req->session_id);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_frame_inject_ioctl() - Handle frame injection IOCTL
 * @dev: Network device
 * @ifr: Interface request structure
 * @cmd: IOCTL command
 *
 * This function handles frame injection IOCTL requests from userspace.
 *
 * Return: 0 on success, negative error code on failure
 */
int hdd_frame_inject_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct hdd_adapter *adapter;
	struct hdd_context *hdd_ctx;
	struct hdd_frame_inject_ioctl ioctl_data;
	struct inject_frame_req *req = NULL;
	QDF_STATUS status;
	int ret = 0;

	hdd_inject_info("Frame injection IOCTL called: cmd=0x%x", cmd);
	printk(KERN_INFO "FRAME_INJECT: ioctl called with cmd=0x%x\n", cmd);

	if (!dev || !ifr || !ifr->ifr_data) {
		hdd_inject_err("Invalid parameters");
		return -EINVAL;
	}

	adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	if (!adapter) {
		hdd_inject_err("Invalid adapter");
		return -EINVAL;
	}

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret) {
		hdd_inject_err("Invalid HDD context: %d", ret);
		return ret;
	}

	/* Check if injection is supported */
	if (!adapter->injection_ctx) {
		hdd_inject_err("Frame injection not initialized");
		return -EOPNOTSUPP;
	}

	/* Validate command */
	if (cmd != SIOCDEVPRIVATE_FRAME_INJECT) {
		hdd_inject_err("Invalid IOCTL command: 0x%x", cmd);
		return -EINVAL;
	}

	/* Copy IOCTL data from userspace */
	if (copy_from_user(&ioctl_data, ifr->ifr_data, sizeof(ioctl_data))) {
		hdd_inject_err("Failed to copy IOCTL data from userspace");
		return -EFAULT;
	}

	/* Create injection request */
	status = hdd_create_injection_request(&ioctl_data, &req);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_err("Failed to create injection request: %d", status);
		return qdf_status_to_os_return(status);
	}

	/* Process injection request */
	status = hdd_process_frame_injection(adapter, req);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_err("Failed to process injection request: %d", status);
		
		/* Cleanup on failure */
		if (req) {
			if (req->frame_data)
				qdf_mem_free(req->frame_data);
			qdf_mem_free(req);
		}
		
		return qdf_status_to_os_return(status);
	}

	hdd_inject_info("Frame injection IOCTL completed successfully");
	return 0;
}

/**
 * hdd_process_injection_queue_work() - Work function to process injection queue
 * @arg: Work argument (injection context)
 *
 * This function processes queued frame injection requests.
 */
void hdd_process_injection_queue_work(void *arg)
{
	struct hdd_injection_ctx *injection_ctx = (struct hdd_injection_ctx *)arg;
	struct inject_frame_req *req;
	struct net_device *tx_dev;
	qdf_list_node_t *node;
	void *soc;
	QDF_STATUS status;
	uint8_t tx_vdev_id;
	uint8_t cdp_mon_vdev_id;
	uint8_t mon_adapter_vdev_id;
	bool mon_adapter_open;
	bool monitor_mode_active;
	uint64_t total_latency;
	QDF_STATUS wma_status;

	if (!injection_ctx) {
		hdd_inject_err("Invalid injection context");
		return;
	}

	hdd_inject_debug("Processing injection queue work");

	/* Process all queued requests */
	while (true) {
		qdf_spin_lock_bh(&injection_ctx->queue_lock);
		status = qdf_list_remove_front(&injection_ctx->injection_queue, &node);
		qdf_spin_unlock_bh(&injection_ctx->queue_lock);

		if (QDF_IS_STATUS_ERROR(status))
			break;

		req = qdf_container_of(node, struct inject_frame_req, node);

		/* Update timing for processing start */
		req->process_time = qdf_get_log_timestamp();

		/* Send frame to WMA layer for transmission */
		if (injection_ctx->wma_handle) {
			tx_vdev_id = injection_ctx->adapter->vdev_id;
			cdp_mon_vdev_id = 0xff;
			mon_adapter_vdev_id = 0xff;
			mon_adapter_open = false;
			monitor_mode_active = injection_ctx->is_monitor_mode;
			if (!monitor_mode_active && injection_ctx->adapter) {
				tx_dev = injection_ctx->adapter->dev;
				if (injection_ctx->adapter->device_mode ==
						QDF_MONITOR_MODE ||
						(tx_dev && tx_dev->ieee80211_ptr &&
						tx_dev->ieee80211_ptr->iftype ==
						NL80211_IFTYPE_MONITOR)) {
					monitor_mode_active = true;
					injection_ctx->is_monitor_mode = true;
				}
			}

			/*
			 * For monitor-mode injection, prefer monitor vdev id from
			 * datapath. Some userspace monitor workflows keep adapter
			 * vdev as STA while monitor vdev is separate.
			 */
			if (monitor_mode_active) {
				struct hdd_context *hdd_ctx;
				struct hdd_adapter *mon_adapter;

				hdd_ctx = WLAN_HDD_GET_CTX(injection_ctx->adapter);
				mon_adapter = hdd_ctx ?
					hdd_get_adapter(hdd_ctx, QDF_MONITOR_MODE) :
					NULL;
				if (mon_adapter) {
					mon_adapter_vdev_id = mon_adapter->vdev_id;
					mon_adapter_open =
						test_bit(DEVICE_IFACE_OPENED,
							 &mon_adapter->event_flags);
				}

				soc = cds_get_context(QDF_MODULE_ID_SOC);
				if (soc)
					cdp_mon_vdev_id =
						cdp_get_mon_vdev_from_pdev(soc,
								     OL_TXRX_PDEV_ID);

				if (mon_adapter_open &&
				    mon_adapter_vdev_id != 0xff &&
				    mon_adapter_vdev_id != (uint8_t)-EINVAL) {
					tx_vdev_id = mon_adapter_vdev_id;
				} else if (cdp_mon_vdev_id != 0xff &&
					   cdp_mon_vdev_id != (uint8_t)-EINVAL) {
					tx_vdev_id = cdp_mon_vdev_id;
				}
			}

			wma_status = wma_queue_injection_frame(
				(tp_wma_handle)injection_ctx->wma_handle, req,
				tx_vdev_id);

			/* Update timing for completion */
			req->complete_time = qdf_get_log_timestamp();

			if (QDF_IS_STATUS_SUCCESS(wma_status)) {
				hdd_update_injection_stats(injection_ctx->adapter, HDD_INJECTION_STAT_FRAMES_TRANSMITTED, 1);

				/* Calculate and update latency statistics */
				total_latency = req->complete_time - req->submit_time;
				hdd_update_injection_latency(injection_ctx->adapter, total_latency);

				hdd_inject_debug("Frame queued to WMA successfully: session_id=%u, latency=%llu us",
						 req->session_id, total_latency);
			} else {
				hdd_update_injection_stats(injection_ctx->adapter, HDD_INJECTION_STAT_FRAMES_DROPPED, 1);
				hdd_inject_err("Failed to queue frame to WMA: %d", wma_status);
			}
		} else {
			/* Fallback: just update statistics if WMA handle not available */
			req->complete_time = qdf_get_log_timestamp();
			hdd_update_injection_stats(injection_ctx->adapter, HDD_INJECTION_STAT_FRAMES_TRANSMITTED, 1);

			/* Calculate and update latency statistics */
			total_latency = req->complete_time - req->submit_time;
			hdd_update_injection_latency(injection_ctx->adapter, total_latency);

			hdd_inject_warn("WMA handle not available, simulating transmission");
		}

		hdd_inject_debug("Processed injection request: session_id=%u",
				 req->session_id);

		/* Cleanup request */
		if (req->frame_data)
			qdf_mem_free(req->frame_data);
		qdf_mem_free(req);
	}

	hdd_inject_debug("Injection queue processing completed");
}

/**
 * hdd_frame_inject_netlink() - Handle frame injection netlink message
 * @wiphy: Wiphy structure
 * @wdev: Wireless device
 * @data: Netlink data
 * @data_len: Length of netlink data
 *
 * This function handles frame injection requests via cfg80211 vendor commands.
 *
 * Return: 0 on success, negative error code on failure
 */
int hdd_frame_inject_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
			     const void *data, int data_len)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(wdev->netdev);
	struct nlattr *tb[HDD_FRAME_INJECT_ATTR_MAX + 1];
	struct inject_frame_req *req = NULL;
	uint8_t *frame_data = NULL;
	uint32_t frame_len = 0;
	uint32_t tx_flags = 0;
	uint8_t retry_count = 0;
	uint32_t tx_rate = 0;
	QDF_STATUS status;
	int ret = 0;

	hdd_inject_debug("Frame injection netlink command received");

	if (!hdd_ctx || !adapter) {
		hdd_inject_err("Invalid context or adapter");
		return -EINVAL;
	}

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret) {
		hdd_inject_err("Invalid HDD context: %d", ret);
		return ret;
	}

	/* Check if injection is supported */
	if (!adapter->injection_ctx) {
		hdd_inject_err("Frame injection not initialized");
		return -EOPNOTSUPP;
	}

	/* Parse netlink attributes */
	if (wlan_cfg80211_nla_parse(tb, HDD_FRAME_INJECT_ATTR_MAX, data, data_len, NULL)) {
		hdd_inject_err("Failed to parse netlink attributes");
		return -EINVAL;
	}

	/* Extract frame data */
	if (!tb[HDD_FRAME_INJECT_ATTR_FRAME_DATA] || 
	    !tb[HDD_FRAME_INJECT_ATTR_FRAME_LEN]) {
		hdd_inject_err("Missing required frame data attributes");
		return -EINVAL;
	}

	frame_len = nla_get_u32(tb[HDD_FRAME_INJECT_ATTR_FRAME_LEN]);
	if (frame_len == 0 || frame_len > HDD_FRAME_INJECT_MAX_SIZE) {
		hdd_inject_err("Invalid frame length: %u", frame_len);
		return -EINVAL;
	}

	if (nla_len(tb[HDD_FRAME_INJECT_ATTR_FRAME_DATA]) != frame_len) {
		hdd_inject_err("Frame data length mismatch: %u != %u",
			       nla_len(tb[HDD_FRAME_INJECT_ATTR_FRAME_DATA]), frame_len);
		return -EINVAL;
	}

	/* Extract optional parameters */
	if (tb[HDD_FRAME_INJECT_ATTR_TX_FLAGS])
		tx_flags = nla_get_u32(tb[HDD_FRAME_INJECT_ATTR_TX_FLAGS]);

	if (tb[HDD_FRAME_INJECT_ATTR_RETRY_COUNT])
		retry_count = nla_get_u8(tb[HDD_FRAME_INJECT_ATTR_RETRY_COUNT]);

	if (tb[HDD_FRAME_INJECT_ATTR_TX_RATE])
		tx_rate = nla_get_u32(tb[HDD_FRAME_INJECT_ATTR_TX_RATE]);

	/* Allocate injection request */
	req = qdf_mem_malloc(sizeof(*req));
	if (!req) {
		hdd_inject_err("Failed to allocate injection request");
		return -ENOMEM;
	}

	/* Allocate and copy frame data */
	frame_data = qdf_mem_malloc(frame_len);
	if (!frame_data) {
		hdd_inject_err("Failed to allocate frame data buffer");
		qdf_mem_free(req);
		return -ENOMEM;
	}

	qdf_mem_copy(frame_data, nla_data(tb[HDD_FRAME_INJECT_ATTR_FRAME_DATA]), frame_len);

	/* Initialize injection request */
	req->frame_len = frame_len;
	req->frame_data = frame_data;
	req->tx_flags = tx_flags;
	req->retry_count = retry_count;
	req->tx_rate = tx_rate;
	req->timestamp = qdf_get_log_timestamp();
	req->session_id = hdd_get_next_session_id();
	/* Initialize timing fields for performance monitoring */
	req->submit_time = qdf_get_log_timestamp();
	req->queue_time = 0;
	req->process_time = 0;
	req->complete_time = 0;

	/* Process injection request */
	status = hdd_process_frame_injection(adapter, req);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_err("Failed to process injection request: %d", status);
		
		/* Cleanup on failure */
		qdf_mem_free(frame_data);
		qdf_mem_free(req);
		
		return qdf_status_to_os_return(status);
	}

	hdd_inject_info("Frame injection netlink command completed successfully");
	return 0;
}

/**
 * hdd_get_injection_stats_netlink() - Get injection statistics via netlink
 * @wiphy: Wiphy structure
 * @wdev: Wireless device
 * @data: Netlink data
 * @data_len: Length of netlink data
 *
 * This function returns injection statistics via cfg80211 vendor commands.
 *
 * Return: 0 on success, negative error code on failure
 */
int hdd_get_injection_stats_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
				    const void *data, int data_len)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(wdev->netdev);
	struct injection_stats stats;
	struct sk_buff *reply_skb;
	QDF_STATUS status;
	int ret = 0;

	hdd_inject_debug("Get injection stats netlink command received");

	if (!hdd_ctx || !adapter) {
		hdd_inject_err("Invalid context or adapter");
		return -EINVAL;
	}

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret) {
		hdd_inject_err("Invalid HDD context: %d", ret);
		return ret;
	}

	/* Check if injection is supported */
	if (!adapter->injection_ctx) {
		hdd_inject_err("Frame injection not initialized");
		return -EOPNOTSUPP;
	}

	/* Get injection statistics */
	status = hdd_get_injection_stats(adapter, &stats);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_err("Failed to get injection stats: %d", status);
		return qdf_status_to_os_return(status);
	}

	/* Allocate reply buffer */
	reply_skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(stats) + 100);
	if (!reply_skb) {
		hdd_inject_err("Failed to allocate reply buffer");
		return -ENOMEM;
	}

	/* Add statistics to reply */
	if (nla_put(reply_skb, HDD_FRAME_INJECT_ATTR_STATS, sizeof(stats), &stats)) {
		hdd_inject_err("Failed to add stats to reply");
		kfree_skb(reply_skb);
		return -EMSGSIZE;
	}

	ret = cfg80211_vendor_cmd_reply(reply_skb);
	if (ret) {
		hdd_inject_err("Failed to send reply: %d", ret);
		return ret;
	}

	hdd_inject_info("Injection stats sent successfully");
	return 0;
}

/**
 * hdd_reset_injection_stats_netlink() - Reset injection statistics via netlink
 * @wiphy: Wiphy structure
 * @wdev: Wireless device
 * @data: Netlink data
 * @data_len: Length of netlink data
 *
 * This function resets injection statistics via cfg80211 vendor commands.
 *
 * Return: 0 on success, negative error code on failure
 */
int hdd_reset_injection_stats_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
				      const void *data, int data_len)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(wdev->netdev);
	QDF_STATUS status;
	int ret = 0;

	hdd_inject_debug("Reset injection stats netlink command received");

	if (!hdd_ctx || !adapter) {
		hdd_inject_err("Invalid context or adapter");
		return -EINVAL;
	}

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret) {
		hdd_inject_err("Invalid HDD context: %d", ret);
		return ret;
	}

	/* Check if injection is supported */
	if (!adapter->injection_ctx) {
		hdd_inject_err("Frame injection not initialized");
		return -EOPNOTSUPP;
	}

	/* Reset injection statistics */
	status = hdd_reset_injection_stats(adapter);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_inject_err("Failed to reset injection stats: %d", status);
		return qdf_status_to_os_return(status);
	}

	hdd_inject_info("Injection stats reset successfully");
	return 0;
}

/**
 * hdd_recover_from_injection_error() - Recover from injection error
 * @adapter: HDD adapter
 * @error_type: Type of error that occurred
 * @error_code: Specific error code
 * @frame_req: Frame request that caused error (optional)
 *
 * This function implements error recovery mechanisms for frame injection
 * failures. It handles different error types with appropriate recovery
 * strategies and implements graceful degradation under resource pressure.
 *
 * Return: QDF_STATUS_SUCCESS on successful recovery, error code on failure
 */
QDF_STATUS hdd_recover_from_injection_error(struct hdd_adapter *adapter,
					     enum hdd_injection_error_type error_type,
					     int32_t error_code,
					     struct inject_frame_req *frame_req)
{
	struct hdd_injection_ctx *injection_ctx;
	struct hdd_injection_recovery_ctx *recovery_ctx;
	struct hdd_injection_error_info *error_info;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	uint64_t current_time;

	hdd_inject_debug("Starting error recovery: type=%d, code=%d", error_type, error_code);

	if (!adapter || !adapter->injection_ctx) {
		hdd_inject_err("Invalid adapter or injection context");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	recovery_ctx = &injection_ctx->recovery_ctx;
	error_info = &recovery_ctx->last_error;
	current_time = qdf_get_log_timestamp();

	/* Check if recovery is already in progress */
	if (recovery_ctx->recovery_in_progress) {
		hdd_inject_warn("Recovery already in progress, queuing error");
		recovery_ctx->consecutive_errors++;
		return QDF_STATUS_E_BUSY;
	}

	/* Record error information */
	error_info->error_type = error_type;
	error_info->error_code = error_code;
	error_info->timestamp = current_time;
	error_info->frame_len = frame_req ? frame_req->frame_len : 0;
	error_info->retry_count = 0;
	error_info->recovery_attempted = true;

	/* Set recovery in progress flag */
	recovery_ctx->recovery_in_progress = true;
	recovery_ctx->recovery_start_time = current_time;
	recovery_ctx->recovery_attempts++;
	recovery_ctx->consecutive_errors++;

	/* Generate error description */
	switch (error_type) {
	case HDD_INJECTION_ERROR_VALIDATION:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Frame validation failed: code=%d", error_code);
		break;
	case HDD_INJECTION_ERROR_PERMISSION:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Permission denied: code=%d", error_code);
		break;
	case HDD_INJECTION_ERROR_RATE_LIMIT:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Rate limit exceeded: code=%d", error_code);
		break;
	case HDD_INJECTION_ERROR_QUEUE_FULL:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Injection queue full: code=%d", error_code);
		break;
	case HDD_INJECTION_ERROR_FIRMWARE:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Firmware error: code=%d", error_code);
		break;
	case HDD_INJECTION_ERROR_MEMORY:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Memory allocation failed: code=%d", error_code);
		break;
	case HDD_INJECTION_ERROR_INTERFACE:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Interface not ready: code=%d", error_code);
		break;
	case HDD_INJECTION_ERROR_TIMEOUT:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Operation timeout: code=%d", error_code);
		break;
	default:
		snprintf(error_info->description, sizeof(error_info->description),
			 "Unknown error: type=%d, code=%d", error_type, error_code);
		break;
	}

	hdd_inject_warn("Injection error: %s", error_info->description);

	/* Implement recovery strategy based on error type */
	switch (error_type) {
	case HDD_INJECTION_ERROR_VALIDATION:
	case HDD_INJECTION_ERROR_PERMISSION:
		/* These are user errors, no recovery needed */
		status = QDF_STATUS_SUCCESS;
		break;

	case HDD_INJECTION_ERROR_RATE_LIMIT:
		/* Reset rate limiting counters */
		injection_ctx->security_ctx.current_rate_count = 0;
		injection_ctx->security_ctx.rate_limit_start_time = current_time;
		status = QDF_STATUS_SUCCESS;
		break;

	case HDD_INJECTION_ERROR_QUEUE_FULL:
		/* Implement queue cleanup and backpressure */
		status = hdd_handle_injection_degradation(adapter, 1 /* queue pressure */);
		break;

	case HDD_INJECTION_ERROR_FIRMWARE:
		/* Schedule firmware error recovery work */
		qdf_sched_work(0, &recovery_ctx->recovery_work);
		status = QDF_STATUS_E_PENDING;
		break;

	case HDD_INJECTION_ERROR_MEMORY:
		/* Implement memory pressure handling */
		status = hdd_handle_injection_degradation(adapter, 2 /* memory pressure */);
		break;

	case HDD_INJECTION_ERROR_INTERFACE:
		/* Reset interface state */
		status = hdd_reset_injection_state(adapter);
		break;

	case HDD_INJECTION_ERROR_TIMEOUT:
		/* Start recovery timer */
		qdf_timer_start(&recovery_ctx->recovery_timer, 5000); /* 5 second timeout */
		status = QDF_STATUS_E_PENDING;
		break;

	default:
		hdd_inject_err("Unknown error type: %d", error_type);
		status = QDF_STATUS_E_INVAL;
		break;
	}

	/* If recovery completed immediately, clear recovery flag */
	if (status != QDF_STATUS_E_PENDING) {
		recovery_ctx->recovery_in_progress = false;
		if (QDF_IS_STATUS_SUCCESS(status)) {
			recovery_ctx->consecutive_errors = 0;
		}
	}

	hdd_inject_info("Error recovery %s: type=%d, status=%d",
			QDF_IS_STATUS_SUCCESS(status) ? "completed" : "initiated",
			error_type, status);

	return status;
}

/**
 * hdd_reset_injection_state() - Reset injection state after error
 * @adapter: HDD adapter
 *
 * This function resets the injection state to a clean state after
 * encountering errors. It clears error flags, resets counters, and
 * prepares the system for normal operation.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_reset_injection_state(struct hdd_adapter *adapter)
{
	struct hdd_injection_ctx *injection_ctx;
	struct hdd_injection_recovery_ctx *recovery_ctx;
	struct inject_frame_req *req;
	qdf_list_node_t *node, *next_node;
	QDF_STATUS status;

	hdd_inject_debug("Resetting injection state for adapter %pK", adapter);

	if (!adapter || !adapter->injection_ctx) {
		hdd_inject_err("Invalid adapter or injection context");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	recovery_ctx = &injection_ctx->recovery_ctx;

	/* Cancel any pending recovery work */
	qdf_cancel_work(&recovery_ctx->recovery_work);
	qdf_timer_stop(&recovery_ctx->recovery_timer);

	/* Clear recovery flags */
	recovery_ctx->recovery_in_progress = false;
	recovery_ctx->consecutive_errors = 0;

	/* Reset security context counters */
	injection_ctx->security_ctx.current_rate_count = 0;
	injection_ctx->security_ctx.rate_limit_start_time = qdf_get_log_timestamp();

	/* Clear injection queue if it has stale entries */
	qdf_spin_lock_bh(&injection_ctx->queue_lock);
	
	status = qdf_list_peek_front(&injection_ctx->injection_queue, &node);
	while (QDF_IS_STATUS_SUCCESS(status)) {
		req = qdf_container_of(node, struct inject_frame_req, node);
		
		/* Check if request is too old (older than 5 seconds) */
		if ((qdf_get_log_timestamp() - req->timestamp) > 5000000) {
			status = qdf_list_peek_next(&injection_ctx->injection_queue, node, &next_node);
			qdf_list_remove_node(&injection_ctx->injection_queue, node);
			
			if (req->frame_data)
				qdf_mem_free(req->frame_data);
			qdf_mem_free(req);
			
			node = next_node;
		} else {
			status = qdf_list_peek_next(&injection_ctx->injection_queue, node, &next_node);
			node = next_node;
		}
	}
	
	qdf_spin_unlock_bh(&injection_ctx->queue_lock);

	hdd_inject_info("Injection state reset completed for adapter %pK", adapter);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_translate_injection_error() - Translate error codes between layers
 * @qdf_status: QDF status code
 * @layer_error: Layer-specific error code
 *
 * This function translates error codes between different layers (HDD, WMA, firmware)
 * to provide consistent error reporting to userspace applications.
 *
 * Return: Standard errno value for userspace
 */
int hdd_translate_injection_error(QDF_STATUS qdf_status, int32_t layer_error)
{
	int errno_val;

	switch (qdf_status) {
	case QDF_STATUS_SUCCESS:
		errno_val = 0;
		break;
	case QDF_STATUS_E_INVAL:
		errno_val = -EINVAL;
		break;
	case QDF_STATUS_E_NOMEM:
		errno_val = -ENOMEM;
		break;
	case QDF_STATUS_E_PERM:
		errno_val = -EPERM;
		break;
	case QDF_STATUS_E_RESOURCES:
		errno_val = -EBUSY;
		break;
	case QDF_STATUS_E_TIMEOUT:
		errno_val = -ETIMEDOUT;
		break;
	case QDF_STATUS_E_NOSUPPORT:
		errno_val = -EOPNOTSUPP;
		break;
	case QDF_STATUS_E_FAULT:
		errno_val = -EFAULT;
		break;
	case QDF_STATUS_E_AGAIN:
		errno_val = -EAGAIN;
		break;
	case QDF_STATUS_E_BUSY:
		errno_val = -EBUSY;
		break;
	case QDF_STATUS_E_CANCELED:
		errno_val = -ECANCELED;
		break;
	default:
		/* For unknown QDF status, use layer-specific error if available */
		if (layer_error != 0) {
			errno_val = layer_error;
		} else {
			errno_val = -EIO; /* Generic I/O error */
		}
		break;
	}

	hdd_inject_debug("Translated QDF status %d (layer_error %d) to errno %d",
			 qdf_status, layer_error, errno_val);

	return errno_val;
}

/**
 * hdd_handle_injection_degradation() - Handle graceful degradation
 * @adapter: HDD adapter
 * @resource_type: Type of resource under pressure
 *
 * This function implements graceful degradation strategies when system
 * resources are under pressure. It may reduce injection rates, queue sizes,
 * or temporarily disable non-critical features.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_handle_injection_degradation(struct hdd_adapter *adapter,
					     uint32_t resource_type)
{
	struct hdd_injection_ctx *injection_ctx;
	struct injection_config *config;
	struct inject_frame_req *req;
	qdf_list_node_t *node;
	QDF_STATUS status;
	uint32_t frames_dropped = 0;

	hdd_inject_debug("Handling injection degradation: resource_type=%u", resource_type);

	if (!adapter || !adapter->injection_ctx) {
		hdd_inject_err("Invalid adapter or injection context");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	config = &injection_ctx->security_ctx.config;

	switch (resource_type) {
	case 1: /* Queue pressure */
		hdd_inject_warn("Queue pressure detected, reducing queue size");
		
		/* Reduce queue size by 50% */
		config->max_queue_size = config->max_queue_size / 2;
		if (config->max_queue_size < 8) {
			config->max_queue_size = 8; /* Minimum queue size */
		}

		/* Drop oldest frames from queue */
		qdf_spin_lock_bh(&injection_ctx->queue_lock);
		while (qdf_list_size(&injection_ctx->injection_queue) > config->max_queue_size) {
			status = qdf_list_remove_front(&injection_ctx->injection_queue, &node);
			if (QDF_IS_STATUS_SUCCESS(status)) {
				req = qdf_container_of(node, struct inject_frame_req, node);
				if (req->frame_data)
					qdf_mem_free(req->frame_data);
				qdf_mem_free(req);
				frames_dropped++;
			} else {
				break;
			}
		}
		qdf_spin_unlock_bh(&injection_ctx->queue_lock);

		injection_ctx->security_ctx.stats.frames_dropped += frames_dropped;
		hdd_inject_info("Dropped %u frames due to queue pressure", frames_dropped);
		break;

	case 2: /* Memory pressure */
		hdd_inject_warn("Memory pressure detected, reducing frame rate");
		
		/* Reduce frame rate by 50% */
		config->max_frame_rate = config->max_frame_rate / 2;
		if (config->max_frame_rate < 10) {
			config->max_frame_rate = 10; /* Minimum frame rate */
		}

		/* Clear current rate limiting window to apply new rate immediately */
		injection_ctx->security_ctx.current_rate_count = 0;
		injection_ctx->security_ctx.rate_limit_start_time = qdf_get_log_timestamp();
		
		hdd_inject_info("Reduced frame rate to %u fps due to memory pressure",
				config->max_frame_rate);
		break;

	case 3: /* CPU pressure */
		hdd_inject_warn("CPU pressure detected, increasing rate window");
		
		/* Increase rate limiting window to reduce CPU load */
		config->rate_window_ms = config->rate_window_ms * 2;
		if (config->rate_window_ms > 10000) {
			config->rate_window_ms = 10000; /* Maximum 10 second window */
		}
		
		hdd_inject_info("Increased rate window to %u ms due to CPU pressure",
				config->rate_window_ms);
		break;

	default:
		hdd_inject_err("Unknown resource type: %u", resource_type);
		return QDF_STATUS_E_INVAL;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_injection_recovery_work() - Work function for error recovery
 * @arg: Work argument (adapter pointer)
 *
 * This function performs error recovery operations in a work context.
 * It handles recovery tasks that may take time or require sleeping.
 */
void hdd_injection_recovery_work(void *arg)
{
	struct hdd_adapter *adapter = (struct hdd_adapter *)arg;
	struct hdd_injection_ctx *injection_ctx;
	struct hdd_injection_recovery_ctx *recovery_ctx;
	QDF_STATUS status;

	hdd_inject_debug("Starting injection recovery work");

	if (!adapter || !adapter->injection_ctx) {
		hdd_inject_err("Invalid adapter or injection context");
		return;
	}

	injection_ctx = adapter->injection_ctx;
	recovery_ctx = &injection_ctx->recovery_ctx;

	/* Perform recovery based on last error type */
	switch (recovery_ctx->last_error.error_type) {
	case HDD_INJECTION_ERROR_FIRMWARE:
		hdd_inject_info("Performing firmware error recovery");
		
		/* Reset injection state */
		status = hdd_reset_injection_state(adapter);
		if (QDF_IS_STATUS_ERROR(status)) {
			hdd_inject_err("Failed to reset injection state: %d", status);
		}

		/* Wait for firmware to stabilize */
		qdf_sleep(1000); /* 1 second */

		/* Try to re-enable injection */
		if (injection_ctx->is_monitor_mode) {
			status = hdd_frame_inject_enable(adapter);
			if (QDF_IS_STATUS_ERROR(status)) {
				hdd_inject_err("Failed to re-enable injection: %d", status);
			}
		}
		break;

	default:
		hdd_inject_warn("No specific recovery action for error type %d",
				recovery_ctx->last_error.error_type);
		break;
	}

	/* Clear recovery in progress flag */
	recovery_ctx->recovery_in_progress = false;
	
	if (recovery_ctx->consecutive_errors > 10) {
		hdd_inject_warn("Too many consecutive errors (%u), disabling injection",
				recovery_ctx->consecutive_errors);
		injection_ctx->security_ctx.config.injection_enabled = false;
	} else {
		recovery_ctx->consecutive_errors = 0;
	}

	hdd_inject_info("Injection recovery work completed");
}

/**
 * hdd_injection_recovery_timer() - Timer callback for recovery timeout
 * @arg: Timer argument (adapter pointer)
 *
 * This function handles recovery timeout events and initiates appropriate
 * recovery actions when recovery operations take too long.
 */
void hdd_injection_recovery_timer(void *arg)
{
	struct hdd_adapter *adapter = (struct hdd_adapter *)arg;
	struct hdd_injection_ctx *injection_ctx;
	struct hdd_injection_recovery_ctx *recovery_ctx;

	hdd_inject_debug("Injection recovery timer expired");

	if (!adapter || !adapter->injection_ctx) {
		hdd_inject_err("Invalid adapter or injection context");
		return;
	}

	injection_ctx = adapter->injection_ctx;
	recovery_ctx = &injection_ctx->recovery_ctx;

	/* Check if recovery is still in progress */
	if (recovery_ctx->recovery_in_progress) {
		uint64_t recovery_duration = qdf_get_log_timestamp() - recovery_ctx->recovery_start_time;
		
		hdd_inject_warn("Recovery timeout after %llu ms, forcing reset",
				recovery_duration / 1000);

		/* Force reset injection state */
		hdd_reset_injection_state(adapter);

		/* Disable injection if too many timeouts */
		if (recovery_ctx->recovery_attempts > 5) {
			hdd_inject_err("Too many recovery attempts (%u), disabling injection",
				       recovery_ctx->recovery_attempts);
			injection_ctx->security_ctx.config.injection_enabled = false;
		}
	}

	hdd_inject_info("Recovery timer handling completed");
}

/**
 * hdd_get_injection_stats() - Get injection statistics for adapter
 * @adapter: HDD adapter
 * @stats: Pointer to statistics structure to fill
 *
 * This function retrieves current injection statistics for the specified
 * adapter including frame counts, error counts, and performance metrics.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_get_injection_stats(struct hdd_adapter *adapter,
				   struct injection_stats *stats)
{
	struct hdd_injection_ctx *injection_ctx;
	struct wma_injection_queue_stats wma_stats;
	QDF_STATUS status;

	if (!adapter || !stats) {
		hdd_inject_err("Invalid parameters: adapter=%pK, stats=%pK", adapter, stats);
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	if (!injection_ctx) {
		hdd_inject_err("Injection context not initialized for adapter %pK", adapter);
		return QDF_STATUS_E_INVAL;
	}

	/* Copy HDD layer statistics */
	qdf_mem_copy(stats, &injection_ctx->security_ctx.stats, sizeof(*stats));

	/* Get WMA layer statistics and merge them */
	if (injection_ctx->wma_handle) {
		status = wma_get_injection_queue_stats(injection_ctx->wma_handle, &wma_stats);
		if (QDF_IS_STATUS_SUCCESS(status)) {
			/* Merge WMA statistics with HDD statistics */
			stats->frames_submitted += wma_stats.frames_queued;
			stats->frames_transmitted += wma_stats.frames_processed;
			stats->frames_dropped += wma_stats.frames_dropped;
			stats->queue_overflows += wma_stats.queue_overflows;
			stats->firmware_errors += wma_stats.fw_errors;
			
			/* Update timing statistics */
			if (wma_stats.frames_processed > 0) {
				stats->total_inject_time += wma_stats.total_queue_time;
			}
		} else {
			hdd_inject_warn("Failed to get WMA statistics: %d", status);
		}
	}

	/* Update last injection time */
	stats->last_inject_time = injection_ctx->security_ctx.last_injection_time;

	hdd_inject_debug("Retrieved injection statistics for adapter %pK", adapter);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_reset_injection_stats() - Reset injection statistics for adapter
 * @adapter: HDD adapter
 *
 * This function resets all injection statistics for the specified adapter
 * to zero. This includes frame counts, error counts, and timing statistics.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_reset_injection_stats(struct hdd_adapter *adapter)
{
	struct hdd_injection_ctx *injection_ctx;
	QDF_STATUS status;

	if (!adapter) {
		hdd_inject_err("Invalid adapter parameter");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	if (!injection_ctx) {
		hdd_inject_err("Injection context not initialized for adapter %pK", adapter);
		return QDF_STATUS_E_INVAL;
	}

	/* Reset HDD layer statistics */
	qdf_mem_zero(&injection_ctx->security_ctx.stats, sizeof(injection_ctx->security_ctx.stats));
	qdf_mem_zero(&injection_ctx->error_stats, sizeof(injection_ctx->error_stats));

	/* Reset WMA layer statistics */
	if (injection_ctx->wma_handle) {
		status = wma_reset_injection_queue_stats(injection_ctx->wma_handle);
		if (QDF_IS_STATUS_ERROR(status)) {
			hdd_inject_warn("Failed to reset WMA statistics: %d", status);
		}
	}

	/* Reset timing information */
	injection_ctx->security_ctx.last_injection_time = 0;
	injection_ctx->security_ctx.rate_limit_start_time = 0;
	injection_ctx->security_ctx.current_rate_count = 0;

	hdd_inject_info("Reset injection statistics for adapter %pK", adapter);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_update_injection_stats() - Update injection statistics
 * @adapter: HDD adapter
 * @stat_type: Type of statistic to update
 * @increment: Value to add to the statistic
 *
 * This function provides a centralized way to update injection statistics
 * with proper locking and validation.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_update_injection_stats(struct hdd_adapter *adapter,
				      uint32_t stat_type, uint64_t increment)
{
	struct hdd_injection_ctx *injection_ctx;
	struct injection_stats *stats;

	if (!adapter) {
		hdd_inject_err("Invalid adapter parameter");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	if (!injection_ctx) {
		hdd_inject_err("Injection context not initialized for adapter %pK", adapter);
		return QDF_STATUS_E_INVAL;
	}

	stats = &injection_ctx->security_ctx.stats;

	/* Update the specified statistic */
	switch (stat_type) {
	case HDD_INJECTION_STAT_FRAMES_SUBMITTED:
		stats->frames_submitted += increment;
		break;
	case HDD_INJECTION_STAT_FRAMES_TRANSMITTED:
		stats->frames_transmitted += increment;
		break;
	case HDD_INJECTION_STAT_FRAMES_DROPPED:
		stats->frames_dropped += increment;
		break;
	case HDD_INJECTION_STAT_VALIDATION_FAILURES:
		stats->validation_failures += increment;
		break;
	case HDD_INJECTION_STAT_PERMISSION_DENIALS:
		stats->permission_denials += increment;
		break;
	case HDD_INJECTION_STAT_RATE_LIMIT_HITS:
		stats->rate_limit_hits += increment;
		break;
	case HDD_INJECTION_STAT_QUEUE_OVERFLOWS:
		stats->queue_overflows += increment;
		break;
	case HDD_INJECTION_STAT_FIRMWARE_ERRORS:
		stats->firmware_errors += increment;
		break;
	default:
		hdd_inject_err("Invalid statistic type: %u", stat_type);
		return QDF_STATUS_E_INVAL;
	}

	/* Update last injection time for frame-related statistics */
	if (stat_type == HDD_INJECTION_STAT_FRAMES_SUBMITTED ||
	    stat_type == HDD_INJECTION_STAT_FRAMES_TRANSMITTED) {
		injection_ctx->security_ctx.last_injection_time = qdf_get_log_timestamp();
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_update_injection_latency() - Update injection latency statistics
 * @adapter: HDD adapter
 * @latency_us: Latency in microseconds
 *
 * This function updates latency statistics including min, max, and average
 * latency measurements for performance monitoring.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_update_injection_latency(struct hdd_adapter *adapter,
					uint64_t latency_us)
{
	struct hdd_injection_ctx *injection_ctx;
	struct injection_stats *stats;

	if (!adapter) {
		hdd_inject_err("Invalid adapter parameter");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	if (!injection_ctx) {
		hdd_inject_err("Injection context not initialized for adapter %pK", adapter);
		return QDF_STATUS_E_INVAL;
	}

	stats = &injection_ctx->security_ctx.stats;

	/* Update min latency */
	if (stats->min_latency_us == 0 || latency_us < stats->min_latency_us) {
		stats->min_latency_us = latency_us;
	}

	/* Update max latency */
	if (latency_us > stats->max_latency_us) {
		stats->max_latency_us = latency_us;
	}

	/* Update total latency for average calculation */
	stats->total_latency_us += latency_us;

	/* Calculate running average */
	if (stats->frames_transmitted > 0) {
		stats->avg_latency_us = stats->total_latency_us / stats->frames_transmitted;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_update_injection_throughput() - Update injection throughput statistics
 * @adapter: HDD adapter
 *
 * This function calculates and updates throughput statistics based on
 * recent frame injection activity.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_update_injection_throughput(struct hdd_adapter *adapter)
{
	struct hdd_injection_ctx *injection_ctx;
	struct injection_stats *stats;
	uint64_t current_time;
	uint64_t time_window_ms = 1000; /* 1 second window */
	static uint64_t last_throughput_update = 0;
	static uint64_t frames_in_window = 0;

	if (!adapter) {
		hdd_inject_err("Invalid adapter parameter");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	if (!injection_ctx) {
		hdd_inject_err("Injection context not initialized for adapter %pK", adapter);
		return QDF_STATUS_E_INVAL;
	}

	stats = &injection_ctx->security_ctx.stats;
	current_time = qdf_get_log_timestamp();

	/* Initialize on first call */
	if (last_throughput_update == 0) {
		last_throughput_update = current_time;
		frames_in_window = 1;
		return QDF_STATUS_SUCCESS;
	}

	frames_in_window++;

	/* Calculate throughput every second */
	if (current_time - last_throughput_update >= time_window_ms * 1000) {
		uint32_t throughput_fps = (frames_in_window * 1000000) / 
					  (current_time - last_throughput_update);
		
		stats->current_throughput_fps = throughput_fps;
		
		/* Update peak throughput */
		if (throughput_fps > stats->peak_throughput_fps) {
			stats->peak_throughput_fps = throughput_fps;
		}

		/* Reset for next window */
		last_throughput_update = current_time;
		frames_in_window = 0;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_monitor_injection_resources() - Monitor resource usage for injection
 * @adapter: HDD adapter
 *
 * This function monitors memory and CPU usage related to frame injection
 * and updates resource usage statistics.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_monitor_injection_resources(struct hdd_adapter *adapter)
{
	struct hdd_injection_ctx *injection_ctx;
	struct injection_stats *stats;
	uint32_t queue_size;
	uint64_t memory_usage = 0;

	if (!adapter) {
		hdd_inject_err("Invalid adapter parameter");
		return QDF_STATUS_E_INVAL;
	}

	injection_ctx = adapter->injection_ctx;
	if (!injection_ctx) {
		hdd_inject_err("Injection context not initialized for adapter %pK", adapter);
		return QDF_STATUS_E_INVAL;
	}

	stats = &injection_ctx->security_ctx.stats;

	/* Monitor queue depth */
	qdf_spin_lock_bh(&injection_ctx->queue_lock);
	queue_size = qdf_list_size(&injection_ctx->injection_queue);
	qdf_spin_unlock_bh(&injection_ctx->queue_lock);

	/* Update max queue depth */
	if (queue_size > stats->max_queue_depth) {
		stats->max_queue_depth = queue_size;
	}

	/* Update queue depth samples for average calculation */
	stats->queue_depth_samples++;

	/* Estimate memory usage */
	memory_usage = queue_size * (sizeof(struct inject_frame_req) + HDD_FRAME_INJECT_MAX_SIZE);
	memory_usage += sizeof(struct hdd_injection_ctx);
	memory_usage += sizeof(struct injection_stats);

	stats->memory_usage_bytes = memory_usage;

	/* CPU usage monitoring would require more complex implementation
	 * For now, we'll set it to 0 as a placeholder */
	stats->cpu_usage_percent = 0;

	return QDF_STATUS_SUCCESS;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */
