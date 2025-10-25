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

#ifndef __WMA_FRAME_INJECT_H
#define __WMA_FRAME_INJECT_H

/**
 * DOC: wma_frame_inject.h
 *
 * WMA layer frame injection queue management APIs
 */

#include <qdf_types.h>
#include <qdf_status.h>

/* Include for complete type definitions */
#include "wma.h"

/* Forward declarations */
struct inject_frame_req;

/*
 * Injection uses a dedicated descriptor-id range to avoid colliding with
 * MGMT_TXRX descriptor-pool ids and to identify completions quickly.
 *
 * Keep this range in a mid window (not the extreme high 0xFxxx range) since
 * some firmware variants are stricter about descriptor-id values.
 */
#define WMA_INJECTION_DESC_ID_BASE 0x2000
#define WMA_INJECTION_DESC_ID_MASK 0x0FFF
#define WMA_IS_INJECTION_DESC_ID(_id) \
	(((_id) & ~WMA_INJECTION_DESC_ID_MASK) == WMA_INJECTION_DESC_ID_BASE)

/**
 * enum wma_injection_fw_error_type - Firmware error types for injection
 * @WMA_INJECTION_FW_ERROR_NONE: No error
 * @WMA_INJECTION_FW_ERROR_TIMEOUT: Firmware response timeout
 * @WMA_INJECTION_FW_ERROR_REJECTED: Frame rejected by firmware
 * @WMA_INJECTION_FW_ERROR_INVALID_VDEV: Invalid VDEV ID
 * @WMA_INJECTION_FW_ERROR_NO_RESOURCES: Firmware out of resources
 * @WMA_INJECTION_FW_ERROR_INTERFACE_DOWN: Interface is down
 * @WMA_INJECTION_FW_ERROR_POWER_SAVE: Device in power save mode
 * @WMA_INJECTION_FW_ERROR_CHANNEL_SWITCH: Channel switch in progress
 * @WMA_INJECTION_FW_ERROR_SCAN_ACTIVE: Scan operation active
 * @WMA_INJECTION_FW_ERROR_UNKNOWN: Unknown firmware error
 * @WMA_INJECTION_FW_ERROR_MAX: Maximum error type
 */
enum wma_injection_fw_error_type {
	WMA_INJECTION_FW_ERROR_NONE = 0,
	WMA_INJECTION_FW_ERROR_TIMEOUT,
	WMA_INJECTION_FW_ERROR_REJECTED,
	WMA_INJECTION_FW_ERROR_INVALID_VDEV,
	WMA_INJECTION_FW_ERROR_NO_RESOURCES,
	WMA_INJECTION_FW_ERROR_INTERFACE_DOWN,
	WMA_INJECTION_FW_ERROR_POWER_SAVE,
	WMA_INJECTION_FW_ERROR_CHANNEL_SWITCH,
	WMA_INJECTION_FW_ERROR_SCAN_ACTIVE,
	WMA_INJECTION_FW_ERROR_UNKNOWN,
	WMA_INJECTION_FW_ERROR_MAX
};

/**
 * struct wma_injection_fw_error_info - Firmware error information
 * @error_type: Type of firmware error
 * @fw_error_code: Firmware-specific error code
 * @timestamp: When the error occurred
 * @vdev_id: VDEV ID associated with error
 * @retry_count: Number of retries attempted
 * @recovery_attempted: Whether recovery was attempted
 */
struct wma_injection_fw_error_info {
	enum wma_injection_fw_error_type error_type;
	uint32_t fw_error_code;
	uint64_t timestamp;
	uint8_t vdev_id;
	uint8_t retry_count;
	bool recovery_attempted;
};

/**
 * struct wma_injection_queue_stats - WMA injection queue statistics
 * @frames_queued: Total frames queued
 * @frames_processed: Total frames processed
 * @frames_dropped: Frames dropped due to queue overflow
 * @queue_overflows: Number of queue overflow events
 * @max_queue_depth: Maximum queue depth reached
 * @total_queue_time: Total time frames spent in queue (microseconds)
 * @fw_errors: Number of firmware errors
 * @fw_timeouts: Number of firmware timeouts
 * @fw_retries: Number of firmware retries
 * @last_fw_error: Information about last firmware error
 */
struct wma_injection_queue_stats {
	uint64_t frames_queued;
	uint64_t frames_processed;
	uint64_t frames_dropped;
	uint64_t queue_overflows;
	uint32_t max_queue_depth;
	uint64_t total_queue_time;
	uint64_t fw_errors;
	uint64_t fw_timeouts;
	uint64_t fw_retries;
	struct wma_injection_fw_error_info last_fw_error;
};

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/**
 * wma_init_injection_queue() - Initialize WMA injection queue
 * @wma_handle: WMA handle
 *
 * This function initializes the injection queue infrastructure in the WMA layer.
 * It creates the queue, initializes locks, and sets up work items for processing.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_init_injection_queue(tp_wma_handle wma_handle);

/**
 * wma_deinit_injection_queue() - Deinitialize WMA injection queue
 * @wma_handle: WMA handle
 *
 * This function cleans up the injection queue infrastructure. It flushes any
 * pending frames, cancels work items, and frees allocated resources.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_deinit_injection_queue(tp_wma_handle wma_handle);

/**
 * wma_injection_pre_stop_cleanup() - Destroy injection helper vdev before
 *                                     monitor mode stop
 * @wma_handle: WMA handle
 *
 * Must be called while WMI is still alive, BEFORE the driver sends
 * VDEV_STOP / VDEV_DELETE for the monitor vdev.  Prevents firmware assert
 * caused by orphaned STA helper vdev during monitor teardown.
 */
void wma_injection_pre_stop_cleanup(tp_wma_handle wma_handle);

/**
 * wma_queue_injection_frame() - Queue frame for injection
 * @wma_handle: WMA handle
 * @req: Frame injection request
 * @vdev_id: VDEV ID for the frame
 *
 * This function queues a frame injection request for processing. The frame
 * will be processed in FIFO order by the queue processing work function.
 * The function implements overflow protection and maintains queue statistics.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_queue_injection_frame(tp_wma_handle wma_handle,
				     struct inject_frame_req *req,
				     uint8_t vdev_id);

/**
 * wma_process_injection_queue() - Process injection queue with traffic coordination
 * @wma_handle: WMA handle
 *
 * This function processes the injection queue while coordinating with existing
 * WMA traffic scheduling and applying backpressure when needed. It implements
 * FIFO processing with traffic coordination and resource management.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_process_injection_queue(tp_wma_handle wma_handle);

/**
 * wma_get_injection_queue_stats() - Get injection queue statistics
 * @wma_handle: WMA handle
 * @stats: Pointer to statistics structure to fill
 *
 * This function retrieves current injection queue statistics including
 * queue depth, processed frames, and error counts.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_get_injection_queue_stats(tp_wma_handle wma_handle,
					 struct wma_injection_queue_stats *stats);

/**
 * wma_reset_injection_queue_stats() - Reset injection queue statistics
 * @wma_handle: WMA handle
 *
 * This function resets all injection queue statistics to zero.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_reset_injection_queue_stats(tp_wma_handle wma_handle);

/**
 * wma_get_injection_queue_size() - Get current queue size
 * @wma_handle: WMA handle
 *
 * This function returns the current number of frames in the injection queue.
 *
 * Return: Current queue size, 0 if queue not initialized
 */
uint32_t wma_get_injection_queue_size(tp_wma_handle wma_handle);

/**
 * wma_is_injection_queue_empty() - Check if injection queue is empty
 * @wma_handle: WMA handle
 *
 * This function checks whether the injection queue is empty.
 *
 * Return: true if queue is empty, false otherwise
 */
bool wma_is_injection_queue_empty(tp_wma_handle wma_handle);

/**
 * wma_flush_injection_queue() - Flush all frames from injection queue
 * @wma_handle: WMA handle
 *
 * This function removes and frees all frames from the injection queue.
 * It also cancels any pending queue processing work.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_flush_injection_queue(tp_wma_handle wma_handle);

/**
 * wma_send_injection_frame_to_fw() - Send injection frame to firmware
 * @wma_handle: WMA handle
 * @req: Frame injection request
 * @vdev_id: VDEV ID for the frame
 *
 * This function sends a validated injection frame to the firmware via WMI.
 * It formats the frame as a WMI management command and handles firmware
 * communication including error reporting and response handling.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_send_injection_frame_to_fw(tp_wma_handle wma_handle,
					  struct inject_frame_req *req,
					  uint8_t vdev_id);

/**
 * wma_handle_injection_fw_response() - Handle firmware response for injection
 * @wma_handle: WMA handle
 * @desc_id: Descriptor ID from firmware completion event
 * @status: Firmware completion status
 *
 * This function processes firmware completion events for injected frames.
 * It updates statistics and handles error reporting based on firmware response.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_handle_injection_fw_response(tp_wma_handle wma_handle,
					     uint32_t desc_id,
					     uint32_t status);

/**
 * wma_handle_firmware_injection_error() - Handle firmware injection error
 * @wma_handle: WMA handle
 * @error_code: Firmware error code
 * @vdev_id: VDEV ID associated with error
 * @req: Frame request that caused error (optional)
 *
 * This function handles firmware errors during frame injection. It implements
 * retry logic for transient failures and coordinates with HDD layer for
 * error recovery. It also maintains firmware state synchronization.
 *
 * Return: QDF_STATUS_SUCCESS on successful recovery, error code on failure
 */
QDF_STATUS wma_handle_firmware_injection_error(tp_wma_handle wma_handle,
						uint32_t error_code,
						uint8_t vdev_id,
						struct inject_frame_req *req);

/**
 * wma_retry_injection_frame() - Retry injection frame after firmware error
 * @wma_handle: WMA handle
 * @req: Frame injection request to retry
 * @vdev_id: VDEV ID for the frame
 * @error_type: Type of error that occurred
 *
 * This function implements retry logic for transient firmware failures.
 * It uses exponential backoff and limits the number of retry attempts.
 *
 * Return: QDF_STATUS_SUCCESS on successful retry, error code on failure
 */
QDF_STATUS wma_retry_injection_frame(tp_wma_handle wma_handle,
				     struct inject_frame_req *req,
				     uint8_t vdev_id,
				     enum wma_injection_fw_error_type error_type);

/**
 * wma_sync_firmware_injection_state() - Synchronize firmware injection state
 * @wma_handle: WMA handle
 * @vdev_id: VDEV ID to synchronize
 *
 * This function synchronizes the firmware injection state after errors.
 * It ensures that the firmware and driver are in a consistent state.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_sync_firmware_injection_state(tp_wma_handle wma_handle,
					      uint8_t vdev_id);

/**
 * wma_translate_fw_injection_error() - Translate firmware error codes
 * @fw_error_code: Firmware-specific error code
 *
 * This function translates firmware-specific error codes to standard
 * WMA injection error types for consistent error handling.
 *
 * Return: Translated error type
 */
enum wma_injection_fw_error_type wma_translate_fw_injection_error(uint32_t fw_error_code);

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline QDF_STATUS wma_init_injection_queue(tp_wma_handle wma_handle)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS wma_deinit_injection_queue(tp_wma_handle wma_handle)
{
	return QDF_STATUS_SUCCESS;
}

static inline void wma_injection_pre_stop_cleanup(tp_wma_handle wma_handle)
{
}

static inline QDF_STATUS wma_queue_injection_frame(tp_wma_handle wma_handle,
						    struct inject_frame_req *req,
						    uint8_t vdev_id)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS wma_process_injection_queue(tp_wma_handle wma_handle)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS wma_get_injection_queue_stats(tp_wma_handle wma_handle,
							struct wma_injection_queue_stats *stats)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS wma_reset_injection_queue_stats(tp_wma_handle wma_handle)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline uint32_t wma_get_injection_queue_size(tp_wma_handle wma_handle)
{
	return 0;
}

static inline bool wma_is_injection_queue_empty(tp_wma_handle wma_handle)
{
	return true;
}

static inline QDF_STATUS wma_flush_injection_queue(tp_wma_handle wma_handle)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS wma_send_injection_frame_to_fw(tp_wma_handle wma_handle,
							struct inject_frame_req *req,
							uint8_t vdev_id)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS wma_handle_injection_fw_response(tp_wma_handle wma_handle,
							   uint32_t desc_id,
							   uint32_t status)
{
	return QDF_STATUS_E_NOSUPPORT;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WMA_FRAME_INJECT_H */
