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
 * DOC: wlan_hdd_inject_security.c
 *
 * WLAN Host Device Driver Frame Injection Security Implementation
 */

#include "wlan_hdd_includes.h"
#include "wlan_hdd_frame_inject.h"
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <qdf_mem.h>
#include <qdf_trace.h>
#include <qdf_time.h>

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/* Logging macros for injection security */
#define hdd_security_debug(params...) \
	QDF_TRACE_DEBUG(QDF_MODULE_ID_HDD, params)
#define hdd_security_info(params...) \
	QDF_TRACE_INFO(QDF_MODULE_ID_HDD, params)
#define hdd_security_warn(params...) \
	QDF_TRACE_WARN(QDF_MODULE_ID_HDD, params)
#define hdd_security_err(params...) \
	QDF_TRACE_ERROR(QDF_MODULE_ID_HDD, params)

/* Default security configuration values */
#define HDD_INJECT_DEFAULT_ENABLED           true
#define HDD_INJECT_DEFAULT_MAX_RATE          HDD_FRAME_INJECT_DEFAULT_RATE_LIMIT
#define HDD_INJECT_DEFAULT_MAX_SIZE          HDD_FRAME_INJECT_MAX_SIZE
#define HDD_INJECT_DEFAULT_MAX_QUEUE         HDD_FRAME_INJECT_MAX_QUEUE_SIZE
#define HDD_INJECT_DEFAULT_RATE_WINDOW       HDD_FRAME_INJECT_RATE_WINDOW_MS
#define HDD_INJECT_DEFAULT_REQUIRE_MONITOR   true
#define HDD_INJECT_DEFAULT_LOG_LEVEL         3

/* Session tracking structure */
struct injection_session {
	uint32_t session_id;
	pid_t pid;
	uid_t uid;
	uint64_t start_time;
	uint32_t frame_count;
	qdf_list_node_t node;
};

/**
 * hdd_get_current_time_ms() - Get current time in milliseconds
 *
 * Return: Current time in milliseconds
 */
static uint64_t hdd_get_current_time_ms(void)
{
	return qdf_get_log_timestamp();
}

/**
 * hdd_init_injection_security_ctx() - Initialize security context
 * @security_ctx: Security context to initialize
 *
 * This function initializes the injection security context with
 * default values and creates necessary data structures.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_init_injection_security_ctx(struct injection_security_ctx *security_ctx)
{
	hdd_security_debug("Initializing injection security context");

	if (!security_ctx) {
		hdd_security_err("Security context is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	/* Initialize configuration with global settings */
	hdd_injection_get_global_config(&security_ctx->config);

	/* Initialize statistics */
	qdf_mem_zero(&security_ctx->stats, sizeof(security_ctx->stats));

	/* Initialize rate limiting */
	security_ctx->rate_limit_start_time = hdd_get_current_time_ms();
	security_ctx->current_rate_count = 0;
	security_ctx->last_injection_time = 0;

	/* Initialize session list */
	qdf_list_create(&security_ctx->active_sessions, 
			HDD_FRAME_INJECT_MAX_QUEUE_SIZE);

	/* Initialize session lock */
	qdf_spinlock_create(&security_ctx->session_lock);

	hdd_security_info("Injection security context initialized successfully");
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_deinit_injection_security_ctx() - Cleanup security context
 * @security_ctx: Security context to cleanup
 *
 * This function cleans up the injection security context and
 * frees all associated resources.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_deinit_injection_security_ctx(struct injection_security_ctx *security_ctx)
{
	struct injection_session *session;
	qdf_list_node_t *node, *next_node;
	QDF_STATUS status;

	hdd_security_debug("Cleaning up injection security context");

	if (!security_ctx) {
		hdd_security_err("Security context is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	/* Clean up active sessions */
	qdf_spin_lock_bh(&security_ctx->session_lock);
	
	status = qdf_list_peek_front(&security_ctx->active_sessions, &node);
	while (QDF_IS_STATUS_SUCCESS(status)) {
		session = qdf_container_of(node, struct injection_session, node);
		
		status = qdf_list_peek_next(&security_ctx->active_sessions, node, &next_node);
		
		qdf_list_remove_node(&security_ctx->active_sessions, node);
		qdf_mem_free(session);
		
		node = next_node;
	}
	
	qdf_spin_unlock_bh(&security_ctx->session_lock);

	/* Destroy session list and lock */
	qdf_list_destroy(&security_ctx->active_sessions);
	qdf_spinlock_destroy(&security_ctx->session_lock);

	/* Clear statistics */
	qdf_mem_zero(&security_ctx->stats, sizeof(security_ctx->stats));

	hdd_security_info("Injection security context cleaned up successfully");
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_check_injection_capability() - Check process capabilities
 * @task: Task structure (NULL for current task)
 *
 * This function checks if the calling process has the required
 * capabilities for frame injection operations.
 *
 * Return: QDF_STATUS_SUCCESS if authorized, error code otherwise
 */
QDF_STATUS hdd_check_injection_capability(struct task_struct *task)
{
	const struct cred *cred;
	bool has_capability = false;

	hdd_security_debug("Checking injection capabilities");

	/* Use current task if none specified */
	if (!task)
		task = current;

	/* Get task credentials */
	cred = get_task_cred(task);
	if (!cred) {
		hdd_security_err("Failed to get task credentials");
		return QDF_STATUS_E_FAILURE;
	}

	/* Check for CAP_NET_RAW capability */
	has_capability = capable(CAP_NET_RAW);
	
	put_cred(cred);

	if (!has_capability) {
		hdd_security_warn("Process lacks CAP_NET_RAW capability (PID: %d, UID: %d)",
				  task->pid, from_kuid(&init_user_ns, task_uid(task)));
		return QDF_STATUS_E_PERM;
	}

	hdd_security_debug("Process has required capabilities (PID: %d, UID: %d)",
			   task->pid, from_kuid(&init_user_ns, task_uid(task)));
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_create_injection_session() - Create new injection session
 * @security_ctx: Security context
 * @session_id: Session identifier
 *
 * This function creates a new injection session for tracking
 * and auditing purposes.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_create_injection_session(struct injection_security_ctx *security_ctx,
					       uint32_t session_id)
{
	struct injection_session *session;
	QDF_STATUS status;

	hdd_security_debug("Creating injection session: %u", session_id);

	session = qdf_mem_malloc(sizeof(*session));
	if (!session) {
		hdd_security_err("Failed to allocate session memory");
		return QDF_STATUS_E_NOMEM;
	}

	/* Initialize session */
	session->session_id = session_id;
	session->pid = current->pid;
	session->uid = from_kuid(&init_user_ns, current_uid());
	session->start_time = hdd_get_current_time_ms();
	session->frame_count = 0;

	/* Add to active sessions list */
	qdf_spin_lock_bh(&security_ctx->session_lock);
	status = qdf_list_insert_back(&security_ctx->active_sessions, &session->node);
	qdf_spin_unlock_bh(&security_ctx->session_lock);

	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_security_err("Failed to add session to list: %d", status);
		qdf_mem_free(session);
		return status;
	}

	hdd_security_debug("Created injection session %u (PID: %d, UID: %u)",
			  session_id, session->pid, session->uid);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_find_injection_session() - Find injection session by ID
 * @security_ctx: Security context
 * @session_id: Session identifier to find
 *
 * This function finds an active injection session by its ID.
 *
 * Return: Pointer to session if found, NULL otherwise
 */
static struct injection_session *hdd_find_injection_session(
	struct injection_security_ctx *security_ctx, uint32_t session_id)
{
	struct injection_session *session;
	qdf_list_node_t *node;
	QDF_STATUS status;

	qdf_spin_lock_bh(&security_ctx->session_lock);
	
	status = qdf_list_peek_front(&security_ctx->active_sessions, &node);
	while (QDF_IS_STATUS_SUCCESS(status)) {
		session = qdf_container_of(node, struct injection_session, node);
		
		if (session->session_id == session_id) {
			qdf_spin_unlock_bh(&security_ctx->session_lock);
			return session;
		}
		
		status = qdf_list_peek_next(&security_ctx->active_sessions, node, &node);
	}
	
	qdf_spin_unlock_bh(&security_ctx->session_lock);
	return NULL;
}

/**
 * hdd_apply_injection_rate_limit() - Apply rate limiting
 * @adapter: HDD adapter
 *
 * This function applies rate limiting to frame injection requests
 * to prevent denial of service attacks.
 *
 * Return: QDF_STATUS_SUCCESS if allowed, error code if rate limited
 */
QDF_STATUS hdd_apply_injection_rate_limit(struct hdd_adapter *adapter)
{
	struct injection_security_ctx *security_ctx;
	uint64_t current_time, time_diff;
	uint32_t max_rate, window_ms;

	if (!adapter || !adapter->injection_ctx) {
		hdd_security_err("Invalid adapter or injection context");
		return QDF_STATUS_E_INVAL;
	}

	security_ctx = &adapter->injection_ctx->security_ctx;
	current_time = hdd_get_current_time_ms();
	max_rate = security_ctx->config.max_frame_rate;
	window_ms = security_ctx->config.rate_window_ms;

	hdd_security_debug("Applying rate limit: current_time=%llu, max_rate=%u, window=%u",
			   current_time, max_rate, window_ms);

	/* Check if we need to reset the rate limiting window */
	time_diff = current_time - security_ctx->rate_limit_start_time;
	if (time_diff >= window_ms) {
		/* Reset rate limiting window */
		security_ctx->rate_limit_start_time = current_time;
		security_ctx->current_rate_count = 0;
		hdd_security_debug("Rate limiting window reset");
	}

	/* Check if rate limit is exceeded */
	if (security_ctx->current_rate_count >= max_rate) {
		security_ctx->stats.rate_limit_hits++;
		hdd_security_warn("Rate limit exceeded: %u >= %u (window: %u ms)",
				  security_ctx->current_rate_count, max_rate, window_ms);
		return QDF_STATUS_E_AGAIN;
	}

	/* Increment rate counter */
	security_ctx->current_rate_count++;
	security_ctx->last_injection_time = current_time;

	hdd_security_debug("Rate limit check passed: count=%u/%u",
			   security_ctx->current_rate_count, max_rate);
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_log_injection_activity() - Log injection activity for audit
 * @adapter: HDD adapter
 * @req: Frame injection request
 *
 * This function logs frame injection activity for security
 * auditing and monitoring purposes.
 */
void hdd_log_injection_activity(struct hdd_adapter *adapter,
				struct inject_frame_req *req)
{
	struct injection_security_ctx *security_ctx;
	struct injection_session *session;
	uint64_t current_time;

	if (!adapter || !adapter->injection_ctx || !req) {
		hdd_security_err("Invalid parameters for activity logging");
		return;
	}

	security_ctx = &adapter->injection_ctx->security_ctx;
	current_time = hdd_get_current_time_ms();

	/* Find or create session */
	session = hdd_find_injection_session(security_ctx, req->session_id);
	if (!session) {
		if (QDF_IS_STATUS_SUCCESS(hdd_create_injection_session(security_ctx, req->session_id))) {
			session = hdd_find_injection_session(security_ctx, req->session_id);
		}
	}

	/* Update session statistics */
	if (session) {
		qdf_spin_lock_bh(&security_ctx->session_lock);
		session->frame_count++;
		qdf_spin_unlock_bh(&security_ctx->session_lock);
	}

	/* Log injection activity based on configured log level */
	if (security_ctx->config.log_level >= 4) {
		hdd_security_info("Frame injection: session=%u, len=%u, flags=0x%x, PID=%d, UID=%u",
				  req->session_id, req->frame_len, req->tx_flags,
				  current->pid, from_kuid(&init_user_ns, current_uid()));
	} else if (security_ctx->config.log_level >= 3) {
		hdd_security_debug("Frame injection: session=%u, len=%u",
				   req->session_id, req->frame_len);
	}

	/* Update global statistics */
	security_ctx->stats.frames_submitted++;
	security_ctx->stats.last_inject_time = current_time;
}

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
					      struct inject_frame_req *req)
{
	struct injection_security_ctx *security_ctx;
	QDF_STATUS status;

	hdd_security_debug("Validating injection permissions");

	if (!adapter || !adapter->injection_ctx || !req) {
		hdd_security_err("Invalid parameters for permission validation");
		return QDF_STATUS_E_INVAL;
	}

	security_ctx = &adapter->injection_ctx->security_ctx;

	/* Check if injection is globally enabled */
	if (!security_ctx->config.injection_enabled) {
		security_ctx->stats.permission_denials++;
		hdd_security_warn("Frame injection is disabled");
		return QDF_STATUS_E_PERM;
	}

	/*
	 * Monitor TX may be executed from softirq context where user credentials
	 * are not meaningful. Keep capability enforcement in process context.
	 */
	if (!in_interrupt() && !in_softirq()) {
		status = hdd_check_injection_capability(NULL);
		if (QDF_IS_STATUS_ERROR(status)) {
			security_ctx->stats.permission_denials++;
			hdd_security_warn("Capability check failed: %d", status);
			return status;
		}
	}

	/* Check monitor mode requirement */
	if (security_ctx->config.require_monitor_mode && 
	    !adapter->injection_ctx->is_monitor_mode) {
		security_ctx->stats.permission_denials++;
		hdd_security_warn("Monitor mode required for injection");
		return QDF_STATUS_E_PERM;
	}

	/* Apply rate limiting */
	status = hdd_apply_injection_rate_limit(adapter);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_security_warn("Rate limiting failed: %d", status);
		return status;
	}

	/* Log injection activity */
	hdd_log_injection_activity(adapter, req);

	hdd_security_debug("Injection permissions validated successfully");
	return QDF_STATUS_SUCCESS;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */
