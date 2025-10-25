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

#ifndef __WLAN_HDD_FRAME_INJECT_H
#define __WLAN_HDD_FRAME_INJECT_H

/**
 * DOC: wlan_hdd_frame_inject.h
 *
 * WLAN Host Device Driver Frame Injection APIs
 */

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <net/genetlink.h>
#include <qdf_types.h>
#include <qdf_status.h>
#include <qdf_nbuf.h>
#include <qdf_list.h>
#include <qdf_lock.h>
#include <qdf_timer.h>
#include <qdf_defer.h>
#include "wlan_hdd_frame_validate.h"
#include "wlan_hdd_inject_security.h"
#include "wlan_hdd_frame_inject_debug.h"

/* Forward declarations */
struct hdd_adapter;
struct hdd_context;
struct wiphy;
struct wireless_dev;

/* Maximum frame size for injection (including 802.11 header) */
#define HDD_FRAME_INJECT_MAX_SIZE        2304

/* Maximum number of frames in injection queue per adapter */
#define HDD_FRAME_INJECT_MAX_QUEUE_SIZE  64

/* Default rate limit: frames per second */
#define HDD_FRAME_INJECT_DEFAULT_RATE_LIMIT  100

/* Rate limiting window in milliseconds */
#define HDD_FRAME_INJECT_RATE_WINDOW_MS  1000

/* Statistics type constants for hdd_update_injection_stats() */
#define HDD_INJECTION_STAT_FRAMES_SUBMITTED     0
#define HDD_INJECTION_STAT_FRAMES_TRANSMITTED   1
#define HDD_INJECTION_STAT_FRAMES_DROPPED       2
#define HDD_INJECTION_STAT_VALIDATION_FAILURES  3
#define HDD_INJECTION_STAT_PERMISSION_DENIALS   4
#define HDD_INJECTION_STAT_RATE_LIMIT_HITS      5
#define HDD_INJECTION_STAT_QUEUE_OVERFLOWS      6
#define HDD_INJECTION_STAT_FIRMWARE_ERRORS      7

/* IOCTL commands for frame injection */
#define SIOCDEVPRIVATE_FRAME_INJECT      (SIOCDEVPRIVATE + 10)

/* Netlink family name for frame injection */
#define HDD_FRAME_INJECT_NL_FAMILY       "hdd_frame_inject"

/* Netlink multicast group */
#define HDD_FRAME_INJECT_NL_MCAST_GRP    "inject_events"

/* Vendor command IDs for frame injection (using available range) */
#define QCA_NL80211_VENDOR_SUBCMD_FRAME_INJECT        200
#define QCA_NL80211_VENDOR_SUBCMD_FRAME_INJECT_STATS  201
#define QCA_NL80211_VENDOR_SUBCMD_FRAME_INJECT_RESET  202

/* Vendor command definitions */
#define FEATURE_FRAME_INJECTION_VENDOR_COMMANDS \
{ \
	.info.vendor_id = QCA_NL80211_VENDOR_ID, \
	.info.subcmd = QCA_NL80211_VENDOR_SUBCMD_FRAME_INJECT, \
	.flags = WIPHY_VENDOR_CMD_NEED_WDEV | \
		 WIPHY_VENDOR_CMD_NEED_NETDEV | \
		 WIPHY_VENDOR_CMD_NEED_RUNNING, \
	.doit = hdd_frame_inject_netlink \
}, \
{ \
	.info.vendor_id = QCA_NL80211_VENDOR_ID, \
	.info.subcmd = QCA_NL80211_VENDOR_SUBCMD_FRAME_INJECT_STATS, \
	.flags = WIPHY_VENDOR_CMD_NEED_WDEV | \
		 WIPHY_VENDOR_CMD_NEED_NETDEV, \
	.doit = hdd_get_injection_stats_netlink \
}, \
{ \
	.info.vendor_id = QCA_NL80211_VENDOR_ID, \
	.info.subcmd = QCA_NL80211_VENDOR_SUBCMD_FRAME_INJECT_RESET, \
	.flags = WIPHY_VENDOR_CMD_NEED_WDEV | \
		 WIPHY_VENDOR_CMD_NEED_NETDEV, \
	.doit = hdd_reset_injection_stats_netlink \
}

/**
 * enum hdd_frame_inject_nl_cmd - Netlink commands for frame injection
 * @HDD_FRAME_INJECT_CMD_UNSPEC: Unspecified command
 * @HDD_FRAME_INJECT_CMD_INJECT: Inject frame command
 * @HDD_FRAME_INJECT_CMD_GET_STATS: Get injection statistics
 * @HDD_FRAME_INJECT_CMD_RESET_STATS: Reset injection statistics
 * @HDD_FRAME_INJECT_CMD_SET_CONFIG: Set injection configuration
 * @HDD_FRAME_INJECT_CMD_GET_CONFIG: Get injection configuration
 * @__HDD_FRAME_INJECT_CMD_MAX: Internal use
 * @HDD_FRAME_INJECT_CMD_MAX: Maximum command value
 */
enum hdd_frame_inject_nl_cmd {
	HDD_FRAME_INJECT_CMD_UNSPEC,
	HDD_FRAME_INJECT_CMD_INJECT,
	HDD_FRAME_INJECT_CMD_GET_STATS,
	HDD_FRAME_INJECT_CMD_RESET_STATS,
	HDD_FRAME_INJECT_CMD_SET_CONFIG,
	HDD_FRAME_INJECT_CMD_GET_CONFIG,
	__HDD_FRAME_INJECT_CMD_MAX,
	HDD_FRAME_INJECT_CMD_MAX = __HDD_FRAME_INJECT_CMD_MAX - 1
};

/**
 * enum hdd_frame_inject_nl_attr - Netlink attributes for frame injection
 * @HDD_FRAME_INJECT_ATTR_UNSPEC: Unspecified attribute
 * @HDD_FRAME_INJECT_ATTR_FRAME_DATA: Frame data buffer
 * @HDD_FRAME_INJECT_ATTR_FRAME_LEN: Frame length
 * @HDD_FRAME_INJECT_ATTR_TX_FLAGS: Transmission flags
 * @HDD_FRAME_INJECT_ATTR_RETRY_COUNT: Number of retries
 * @HDD_FRAME_INJECT_ATTR_TX_RATE: Transmission rate
 * @HDD_FRAME_INJECT_ATTR_STATS: Statistics structure
 * @HDD_FRAME_INJECT_ATTR_CONFIG: Configuration structure
 * @__HDD_FRAME_INJECT_ATTR_MAX: Internal use
 * @HDD_FRAME_INJECT_ATTR_MAX: Maximum attribute value
 */
enum hdd_frame_inject_nl_attr {
	HDD_FRAME_INJECT_ATTR_UNSPEC,
	HDD_FRAME_INJECT_ATTR_FRAME_DATA,
	HDD_FRAME_INJECT_ATTR_FRAME_LEN,
	HDD_FRAME_INJECT_ATTR_TX_FLAGS,
	HDD_FRAME_INJECT_ATTR_RETRY_COUNT,
	HDD_FRAME_INJECT_ATTR_TX_RATE,
	HDD_FRAME_INJECT_ATTR_STATS,
	HDD_FRAME_INJECT_ATTR_CONFIG,
	__HDD_FRAME_INJECT_ATTR_MAX,
	HDD_FRAME_INJECT_ATTR_MAX = __HDD_FRAME_INJECT_ATTR_MAX - 1
};

/**
 * enum hdd_frame_inject_tx_flags - Transmission flags for injected frames
 * @HDD_FRAME_INJECT_TX_NO_ACK: Don't wait for ACK
 * @HDD_FRAME_INJECT_TX_NO_ENCRYPT: Don't encrypt frame
 * @HDD_FRAME_INJECT_TX_NO_CCK_RATE: Don't use CCK rates
 * @HDD_FRAME_INJECT_TX_RTS_CTS: Use RTS/CTS protection
 * @HDD_FRAME_INJECT_TX_USE_RATE: Use specified transmission rate
 */
enum hdd_frame_inject_tx_flags {
	HDD_FRAME_INJECT_TX_NO_ACK     = BIT(0),
	HDD_FRAME_INJECT_TX_NO_ENCRYPT = BIT(1),
	HDD_FRAME_INJECT_TX_NO_CCK_RATE = BIT(2),
	HDD_FRAME_INJECT_TX_RTS_CTS    = BIT(3),
	HDD_FRAME_INJECT_TX_USE_RATE   = BIT(4),
};

/**
 * struct inject_frame_req - Frame injection request structure
 * @frame_len: Length of 802.11 frame
 * @frame_data: Pointer to frame buffer
 * @tx_flags: Transmission flags (enum hdd_frame_inject_tx_flags)
 * @retry_count: Number of retries (0-15)
 * @tx_rate: Transmission rate in 100kbps units (optional)
 * @timestamp: Request timestamp
 * @session_id: Session identifier for tracking
 * @node: List node for queueing
 */
struct inject_frame_req {
	uint32_t frame_len;
	uint8_t *frame_data;
	uint32_t tx_flags;
	uint8_t retry_count;
	uint32_t tx_rate;
	uint64_t timestamp;
	uint32_t session_id;
	qdf_list_node_t node;
	/* Performance monitoring fields */
	uint64_t submit_time;
	uint64_t queue_time;
	uint64_t process_time;
	uint64_t complete_time;
};

/**
 * struct injection_stats - Frame injection statistics
 * @frames_submitted: Total frames submitted for injection
 * @frames_transmitted: Successfully transmitted frames
 * @frames_dropped: Frames dropped due to errors
 * @validation_failures: Frame validation failures
 * @permission_denials: Permission denied count
 * @rate_limit_hits: Rate limiting events
 * @queue_overflows: Queue overflow events
 * @firmware_errors: Firmware rejection count
 * @last_inject_time: Timestamp of last injection
 * @total_inject_time: Total time spent in injection (microseconds)
 */
struct injection_stats {
	uint64_t frames_submitted;
	uint64_t frames_transmitted;
	uint64_t frames_dropped;
	uint64_t validation_failures;
	uint64_t permission_denials;
	uint64_t rate_limit_hits;
	uint64_t queue_overflows;
	uint64_t firmware_errors;
	uint64_t last_inject_time;
	uint64_t total_inject_time;
	/* Performance monitoring fields */
	uint64_t min_latency_us;
	uint64_t max_latency_us;
	uint64_t avg_latency_us;
	uint64_t total_latency_us;
	uint32_t current_throughput_fps;
	uint32_t peak_throughput_fps;
	uint64_t memory_usage_bytes;
	uint32_t cpu_usage_percent;
	uint64_t queue_depth_samples;
	uint32_t max_queue_depth;
};

/**
 * struct injection_config - Frame injection configuration
 * @injection_enabled: Global injection enable flag
 * @max_frame_rate: Maximum frames per second
 * @max_frame_size: Maximum frame size allowed
 * @max_queue_size: Maximum queue size per adapter
 * @rate_window_ms: Rate limiting window in milliseconds
 * @require_monitor_mode: Require monitor mode for injection
 * @log_level: Logging level for injection events
 */
struct injection_config {
	bool injection_enabled;
	uint32_t max_frame_rate;
	uint32_t max_frame_size;
	uint32_t max_queue_size;
	uint32_t rate_window_ms;
	bool require_monitor_mode;
	uint8_t log_level;
};

/**
 * struct injection_security_ctx - Security context for frame injection
 * @rate_limit_start_time: Start time of current rate limiting window
 * @current_rate_count: Current frame count in rate window
 * @last_injection_time: Timestamp of last injection
 * @active_sessions: List of active injection sessions
 * @session_lock: Lock for session management
 * @stats: Injection statistics
 * @config: Injection configuration
 */
struct injection_security_ctx {
	uint64_t rate_limit_start_time;
	uint32_t current_rate_count;
	uint64_t last_injection_time;
	qdf_list_t active_sessions;
	qdf_spinlock_t session_lock;
	struct injection_stats stats;
	struct injection_config config;
};

/**
 * enum hdd_injection_error_type - Types of injection errors
 * @HDD_INJECTION_ERROR_NONE: No error
 * @HDD_INJECTION_ERROR_VALIDATION: Frame validation failure
 * @HDD_INJECTION_ERROR_PERMISSION: Permission denied
 * @HDD_INJECTION_ERROR_RATE_LIMIT: Rate limit exceeded
 * @HDD_INJECTION_ERROR_QUEUE_FULL: Injection queue full
 * @HDD_INJECTION_ERROR_FIRMWARE: Firmware communication error
 * @HDD_INJECTION_ERROR_MEMORY: Memory allocation failure
 * @HDD_INJECTION_ERROR_INTERFACE: Interface not ready
 * @HDD_INJECTION_ERROR_TIMEOUT: Operation timeout
 * @HDD_INJECTION_ERROR_RECOVERY: Error recovery in progress
 * @HDD_INJECTION_ERROR_MAX: Maximum error type
 */
enum hdd_injection_error_type {
	HDD_INJECTION_ERROR_NONE = 0,
	HDD_INJECTION_ERROR_VALIDATION,
	HDD_INJECTION_ERROR_PERMISSION,
	HDD_INJECTION_ERROR_RATE_LIMIT,
	HDD_INJECTION_ERROR_QUEUE_FULL,
	HDD_INJECTION_ERROR_FIRMWARE,
	HDD_INJECTION_ERROR_MEMORY,
	HDD_INJECTION_ERROR_INTERFACE,
	HDD_INJECTION_ERROR_TIMEOUT,
	HDD_INJECTION_ERROR_RECOVERY,
	HDD_INJECTION_ERROR_MAX
};

/**
 * struct hdd_injection_error_info - Error information structure
 * @error_type: Type of error that occurred
 * @error_code: Specific error code (QDF_STATUS or errno)
 * @timestamp: When the error occurred
 * @frame_len: Length of frame that caused error (if applicable)
 * @retry_count: Number of retries attempted
 * @recovery_attempted: Whether recovery was attempted
 * @description: Human readable error description
 */
struct hdd_injection_error_info {
	enum hdd_injection_error_type error_type;
	int32_t error_code;
	uint64_t timestamp;
	uint32_t frame_len;
	uint8_t retry_count;
	bool recovery_attempted;
	char description[128];
};

/**
 * struct hdd_injection_recovery_ctx - Error recovery context
 * @recovery_in_progress: Flag indicating recovery is active
 * @recovery_start_time: When recovery started
 * @recovery_attempts: Number of recovery attempts
 * @last_error: Information about the last error
 * @consecutive_errors: Count of consecutive errors
 * @recovery_timer: Timer for recovery operations
 * @recovery_work: Work item for recovery processing
 */
struct hdd_injection_recovery_ctx {
	bool recovery_in_progress;
	uint64_t recovery_start_time;
	uint32_t recovery_attempts;
	struct hdd_injection_error_info last_error;
	uint32_t consecutive_errors;
	qdf_timer_t recovery_timer;
	qdf_work_t recovery_work;
};

/**
 * struct hdd_injection_ctx - Per-adapter injection context
 * @injection_queue: Queue of pending injection requests
 * @queue_lock: Lock for injection queue
 * @security_ctx: Security and rate limiting context
 * @is_monitor_mode: Flag indicating if adapter is in monitor mode
 * @queue_work: Work item for processing injection queue
 * @adapter: Back pointer to HDD adapter
 * @wma_handle: WMA handle for firmware communication
 * @recovery_ctx: Error recovery context
 * @error_stats: Error statistics and tracking
 */
struct hdd_injection_ctx {
	qdf_list_t injection_queue;
	qdf_spinlock_t queue_lock;
	struct injection_security_ctx security_ctx;
	bool is_monitor_mode;
	qdf_work_t queue_work;
	struct hdd_adapter *adapter;
	void *wma_handle;
	struct hdd_injection_recovery_ctx recovery_ctx;
	struct injection_stats error_stats;
	struct dentry *debugfs_dir;
};

/* IOCTL structure for frame injection */
struct hdd_frame_inject_ioctl {
	uint32_t cmd;
	uint32_t frame_len;
	uint8_t *frame_data;
	uint32_t tx_flags;
	uint8_t retry_count;
	uint32_t tx_rate;
};

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/* Function prototypes */

/**
 * hdd_frame_inject_ioctl() - Handle frame injection IOCTL
 * @dev: Network device
 * @ifr: Interface request structure
 * @cmd: IOCTL command
 *
 * Return: 0 on success, negative error code on failure
 */
int hdd_frame_inject_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

/**
 * hdd_frame_inject_netlink() - Handle frame injection netlink message
 * @wiphy: Wiphy structure
 * @wdev: Wireless device
 * @data: Netlink data
 * @data_len: Length of netlink data
 *
 * Return: 0 on success, negative error code on failure
 */
int hdd_frame_inject_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
			     const void *data, int data_len);

/**
 * hdd_get_injection_stats_netlink() - Get injection statistics via netlink
 * @wiphy: Wiphy structure
 * @wdev: Wireless device
 * @data: Netlink data
 * @data_len: Length of netlink data
 *
 * Return: 0 on success, negative error code on failure
 */
int hdd_get_injection_stats_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
				    const void *data, int data_len);

/**
 * hdd_reset_injection_stats_netlink() - Reset injection statistics via netlink
 * @wiphy: Wiphy structure
 * @wdev: Wireless device
 * @data: Netlink data
 * @data_len: Length of netlink data
 *
 * Return: 0 on success, negative error code on failure
 */
int hdd_reset_injection_stats_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
				      const void *data, int data_len);

/**
 * hdd_init_frame_injection() - Initialize frame injection for adapter
 * @adapter: HDD adapter
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_init_frame_injection(struct hdd_adapter *adapter);

/**
 * hdd_deinit_frame_injection() - Cleanup frame injection for adapter
 * @adapter: HDD adapter
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_deinit_frame_injection(struct hdd_adapter *adapter);

/**
 * hdd_frame_inject_enable() - Enable frame injection for adapter
 * @adapter: HDD adapter
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_frame_inject_enable(struct hdd_adapter *adapter);

/**
 * hdd_frame_inject_disable() - Disable frame injection for adapter
 * @adapter: HDD adapter
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_frame_inject_disable(struct hdd_adapter *adapter);

/**
 * hdd_process_frame_injection() - Process frame injection request
 * @adapter: HDD adapter
 * @req: Frame injection request
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_process_frame_injection(struct hdd_adapter *adapter,
				       struct inject_frame_req *req);

/**
 * hdd_process_injection_queue_work() - Work function to process injection queue
 * @arg: Work argument (injection context)
 *
 * This function processes queued frame injection requests.
 */
void hdd_process_injection_queue_work(void *arg);

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
				   struct injection_stats *stats);

/**
 * hdd_reset_injection_stats() - Reset injection statistics for adapter
 * @adapter: HDD adapter
 *
 * This function resets all injection statistics for the specified adapter
 * to zero. This includes frame counts, error counts, and timing statistics.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_reset_injection_stats(struct hdd_adapter *adapter);

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
				      uint32_t stat_type, uint64_t increment);

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
					uint64_t latency_us);

/**
 * hdd_update_injection_throughput() - Update injection throughput statistics
 * @adapter: HDD adapter
 *
 * This function calculates and updates throughput statistics based on
 * recent frame injection activity.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_update_injection_throughput(struct hdd_adapter *adapter);

/**
 * hdd_monitor_injection_resources() - Monitor resource usage for injection
 * @adapter: HDD adapter
 *
 * This function monitors memory and CPU usage related to frame injection
 * and updates resource usage statistics.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_monitor_injection_resources(struct hdd_adapter *adapter);

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
					     struct inject_frame_req *frame_req);

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
QDF_STATUS hdd_reset_injection_state(struct hdd_adapter *adapter);

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
int hdd_translate_injection_error(QDF_STATUS qdf_status, int32_t layer_error);

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
					     uint32_t resource_type);

/**
 * hdd_injection_recovery_work() - Work function for error recovery
 * @arg: Work argument (adapter pointer)
 *
 * This function performs error recovery operations in a work context.
 * It handles recovery tasks that may take time or require sleeping.
 */
void hdd_injection_recovery_work(void *arg);

/**
 * hdd_injection_recovery_timer() - Timer callback for recovery timeout
 * @arg: Timer argument (adapter pointer)
 *
 * This function handles recovery timeout events and initiates appropriate
 * recovery actions when recovery operations take too long.
 */
void hdd_injection_recovery_timer(void *arg);

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline int hdd_frame_inject_ioctl(struct net_device *dev,
					  struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

static inline int hdd_frame_inject_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
					    const void *data, int data_len)
{
	return -EOPNOTSUPP;
}

static inline int hdd_get_injection_stats_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
						   const void *data, int data_len)
{
	return -EOPNOTSUPP;
}

static inline int hdd_reset_injection_stats_netlink(struct wiphy *wiphy, struct wireless_dev *wdev,
						     const void *data, int data_len)
{
	return -EOPNOTSUPP;
}

static inline QDF_STATUS hdd_init_frame_injection(struct hdd_adapter *adapter)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS hdd_deinit_frame_injection(struct hdd_adapter *adapter)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS hdd_frame_inject_enable(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_frame_inject_disable(struct hdd_adapter *adapter)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS hdd_process_frame_injection(struct hdd_adapter *adapter,
						     struct inject_frame_req *req)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_get_injection_stats(struct hdd_adapter *adapter,
						 struct injection_stats *stats)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_reset_injection_stats(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_update_injection_stats(struct hdd_adapter *adapter,
						    uint32_t stat_type, uint64_t increment)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_update_injection_latency(struct hdd_adapter *adapter,
						      uint64_t latency_us)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_update_injection_throughput(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_monitor_injection_resources(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WLAN_HDD_FRAME_INJECT_H */