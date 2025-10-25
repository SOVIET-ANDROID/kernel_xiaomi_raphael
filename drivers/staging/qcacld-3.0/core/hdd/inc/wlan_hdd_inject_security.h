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

#ifndef __WLAN_HDD_INJECT_SECURITY_H
#define __WLAN_HDD_INJECT_SECURITY_H

/**
 * DOC: wlan_hdd_inject_security.h
 *
 * WLAN Host Device Driver Frame Injection Security APIs
 */

#include <qdf_types.h>
#include <qdf_status.h>
#include <linux/sched.h>

/* Forward declarations */
struct hdd_adapter;
struct inject_frame_req;
struct injection_stats;
struct injection_security_ctx;

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/**
 * hdd_init_injection_security_ctx() - Initialize security context
 * @security_ctx: Security context to initialize
 *
 * This function initializes the injection security context with
 * default values and creates necessary data structures.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_init_injection_security_ctx(struct injection_security_ctx *security_ctx);

/**
 * hdd_deinit_injection_security_ctx() - Cleanup security context
 * @security_ctx: Security context to cleanup
 *
 * This function cleans up the injection security context and
 * frees all associated resources.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_deinit_injection_security_ctx(struct injection_security_ctx *security_ctx);

/**
 * hdd_check_injection_capability() - Check process capabilities
 * @task: Task structure (NULL for current task)
 *
 * This function checks if the calling process has the required
 * capabilities for frame injection operations.
 *
 * Return: QDF_STATUS_SUCCESS if authorized, error code otherwise
 */
QDF_STATUS hdd_check_injection_capability(struct task_struct *task);

/**
 * hdd_apply_injection_rate_limit() - Apply rate limiting
 * @adapter: HDD adapter
 *
 * This function applies rate limiting to frame injection requests
 * to prevent denial of service attacks.
 *
 * Return: QDF_STATUS_SUCCESS if allowed, error code if rate limited
 */
QDF_STATUS hdd_apply_injection_rate_limit(struct hdd_adapter *adapter);

/**
 * hdd_log_injection_activity() - Log injection activity for audit
 * @adapter: HDD adapter
 * @req: Frame injection request
 *
 * This function logs frame injection activity for security
 * auditing and monitoring purposes.
 */
void hdd_log_injection_activity(struct hdd_adapter *adapter,
				struct inject_frame_req *req);

/**
 * hdd_validate_injection_permissions() - Validate injection permissions
 * @adapter: HDD adapter
 * @req: Frame injection request
 *
 * This function performs comprehensive permission validation for
 * frame injection requests including capability checks, mode validation,
 * and rate limiting.
 *
 * Return: QDF_STATUS_SUCCESS if authorized, error code otherwise
 */
QDF_STATUS hdd_validate_injection_permissions(struct hdd_adapter *adapter,
					      struct inject_frame_req *req);

/**
 * hdd_get_injection_stats() - Get injection statistics
 * @adapter: HDD adapter
 * @stats: Output buffer for statistics
 *
 * This function retrieves current injection statistics for
 * monitoring and debugging purposes.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_get_injection_stats(struct hdd_adapter *adapter,
				   struct injection_stats *stats);

/**
 * hdd_reset_injection_stats() - Reset injection statistics
 * @adapter: HDD adapter
 *
 * This function resets injection statistics counters.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_reset_injection_stats(struct hdd_adapter *adapter);

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline QDF_STATUS hdd_init_injection_security_ctx(struct injection_security_ctx *security_ctx)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_deinit_injection_security_ctx(struct injection_security_ctx *security_ctx)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_check_injection_capability(struct task_struct *task)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_apply_injection_rate_limit(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline void hdd_log_injection_activity(struct hdd_adapter *adapter,
					      struct inject_frame_req *req)
{
}

static inline QDF_STATUS hdd_validate_injection_permissions(struct hdd_adapter *adapter,
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

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WLAN_HDD_INJECT_SECURITY_H */