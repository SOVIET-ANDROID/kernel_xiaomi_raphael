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
 * DOC: wlan_hdd_frame_inject_debug.c
 *
 * WLAN Host Device Driver Frame Injection Debug and Diagnostic Interfaces
 */

#include "wlan_hdd_includes.h"
#include "wlan_hdd_frame_inject.h"
#include <linux/debugfs.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <qdf_mem.h>
#include <qdf_trace.h>

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/* Debug logging levels */
#define HDD_INJECT_DEBUG_LEVEL_NONE    0
#define HDD_INJECT_DEBUG_LEVEL_ERROR   1
#define HDD_INJECT_DEBUG_LEVEL_WARN    2
#define HDD_INJECT_DEBUG_LEVEL_INFO    3
#define HDD_INJECT_DEBUG_LEVEL_DEBUG   4
#define HDD_INJECT_DEBUG_LEVEL_VERBOSE 5

/* Global debug level */
static uint8_t g_injection_debug_level = HDD_INJECT_DEBUG_LEVEL_INFO;

/* Global configuration parameters */
static bool g_injection_global_enable = true;
static uint32_t g_injection_max_frame_rate = HDD_FRAME_INJECT_DEFAULT_RATE_LIMIT;
static uint32_t g_injection_max_frame_size = HDD_FRAME_INJECT_MAX_SIZE;
static uint32_t g_injection_max_queue_size = HDD_FRAME_INJECT_MAX_QUEUE_SIZE;
static uint32_t g_injection_rate_window_ms = HDD_FRAME_INJECT_RATE_WINDOW_MS;
static bool g_injection_require_monitor_mode = false;

/* Debugfs root directory */
static struct dentry *g_injection_debugfs_root = NULL;

/* Sysfs kobject */
static struct kobject *g_injection_sysfs_kobj = NULL;

/**
 * hdd_injection_debugfs_stats_show() - Show injection statistics in debugfs
 * @file: File pointer
 * @buf: User buffer
 * @count: Buffer size
 * @ppos: File position
 *
 * This function displays injection statistics in debugfs.
 *
 * Return: Number of bytes read, or error code
 */
static ssize_t hdd_injection_debugfs_stats_show(struct file *file,
						 char __user *buf,
						 size_t count,
						 loff_t *ppos)
{
	struct hdd_adapter *adapter = file->private_data;
	struct hdd_injection_ctx *injection_ctx;
	struct injection_stats *stats;
	char *debug_buf;
	int len = 0;
	ssize_t ret;

	if (!adapter || !adapter->injection_ctx) {
		return -EINVAL;
	}

	injection_ctx = adapter->injection_ctx;
	stats = &injection_ctx->security_ctx.stats;

	debug_buf = qdf_mem_malloc(2048);
	if (!debug_buf) {
		return -ENOMEM;
	}

	len += scnprintf(debug_buf + len, 2048 - len,
			 "Frame Injection Statistics for %s:\n", adapter->dev->name);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "================================\n");
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Frames Submitted:     %llu\n", stats->frames_submitted);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Frames Transmitted:   %llu\n", stats->frames_transmitted);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Frames Dropped:       %llu\n", stats->frames_dropped);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Validation Failures:  %llu\n", stats->validation_failures);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Permission Denials:   %llu\n", stats->permission_denials);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Rate Limit Hits:      %llu\n", stats->rate_limit_hits);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Queue Overflows:      %llu\n", stats->queue_overflows);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Firmware Errors:      %llu\n", stats->firmware_errors);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Last Inject Time:     %llu\n", stats->last_inject_time);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Total Inject Time:    %llu us\n", stats->total_inject_time);

	/* Add recovery context information */
	len += scnprintf(debug_buf + len, 2048 - len,
			 "\nError Recovery Information:\n");
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Recovery In Progress: %s\n",
			 injection_ctx->recovery_ctx.recovery_in_progress ? "Yes" : "No");
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Recovery Attempts:    %u\n",
			 injection_ctx->recovery_ctx.recovery_attempts);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Consecutive Errors:   %u\n",
			 injection_ctx->recovery_ctx.consecutive_errors);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Last Error Type:      %d\n",
			 injection_ctx->recovery_ctx.last_error.error_type);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Last Error Code:      %d\n",
			 injection_ctx->recovery_ctx.last_error.error_code);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Last Error Time:      %llu\n",
			 injection_ctx->recovery_ctx.last_error.timestamp);
	len += scnprintf(debug_buf + len, 2048 - len,
			 "Last Error Desc:      %s\n",
			 injection_ctx->recovery_ctx.last_error.description);

	ret = simple_read_from_buffer(buf, count, ppos, debug_buf, len);
	qdf_mem_free(debug_buf);

	return ret;
}

/**
 * hdd_injection_debugfs_config_show() - Show injection configuration in debugfs
 * @file: File pointer
 * @buf: User buffer
 * @count: Buffer size
 * @ppos: File position
 *
 * This function displays injection configuration in debugfs.
 *
 * Return: Number of bytes read, or error code
 */
static ssize_t hdd_injection_debugfs_config_show(struct file *file,
						  char __user *buf,
						  size_t count,
						  loff_t *ppos)
{
	struct hdd_adapter *adapter = file->private_data;
	struct hdd_injection_ctx *injection_ctx;
	struct injection_config *config;
	char *debug_buf;
	int len = 0;
	ssize_t ret;

	if (!adapter || !adapter->injection_ctx) {
		return -EINVAL;
	}

	injection_ctx = adapter->injection_ctx;
	config = &injection_ctx->security_ctx.config;

	debug_buf = qdf_mem_malloc(1024);
	if (!debug_buf) {
		return -ENOMEM;
	}

	len += scnprintf(debug_buf + len, 1024 - len,
			 "Frame Injection Configuration for %s:\n", adapter->dev->name);
	len += scnprintf(debug_buf + len, 1024 - len,
			 "=====================================\n");
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Injection Enabled:    %s\n", config->injection_enabled ? "Yes" : "No");
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Max Frame Rate:       %u fps\n", config->max_frame_rate);
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Max Frame Size:       %u bytes\n", config->max_frame_size);
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Max Queue Size:       %u frames\n", config->max_queue_size);
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Rate Window:          %u ms\n", config->rate_window_ms);
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Require Monitor Mode: %s\n", config->require_monitor_mode ? "Yes" : "No");
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Log Level:            %u\n", config->log_level);
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Monitor Mode Active:  %s\n", injection_ctx->is_monitor_mode ? "Yes" : "No");
	len += scnprintf(debug_buf + len, 1024 - len,
			 "Current Queue Size:   %u frames\n",
			 qdf_list_size(&injection_ctx->injection_queue));

	ret = simple_read_from_buffer(buf, count, ppos, debug_buf, len);
	qdf_mem_free(debug_buf);

	return ret;
}

/**
 * hdd_injection_debugfs_reset_write() - Reset injection statistics via debugfs
 * @file: File pointer
 * @buf: User buffer
 * @count: Buffer size
 * @ppos: File position
 *
 * This function resets injection statistics when written to.
 *
 * Return: Number of bytes written, or error code
 */
static ssize_t hdd_injection_debugfs_reset_write(struct file *file,
						  const char __user *buf,
						  size_t count,
						  loff_t *ppos)
{
	struct hdd_adapter *adapter = file->private_data;
	QDF_STATUS status;

	if (!adapter || !adapter->injection_ctx) {
		return -EINVAL;
	}

	status = hdd_reset_injection_stats(adapter);
	if (QDF_IS_STATUS_ERROR(status)) {
		return -EIO;
	}

	return count;
}

/* Debugfs file operations */
static const struct file_operations hdd_injection_debugfs_stats_fops = {
	.open = simple_open,
	.read = hdd_injection_debugfs_stats_show,
	.llseek = default_llseek,
};

static const struct file_operations hdd_injection_debugfs_config_fops = {
	.open = simple_open,
	.read = hdd_injection_debugfs_config_show,
	.llseek = default_llseek,
};

static const struct file_operations hdd_injection_debugfs_reset_fops = {
	.open = simple_open,
	.write = hdd_injection_debugfs_reset_write,
	.llseek = default_llseek,
};

/**
 * hdd_injection_sysfs_debug_level_show() - Show debug level via sysfs
 * @kobj: Kobject pointer
 * @attr: Attribute pointer
 * @buf: Buffer to write to
 *
 * Return: Number of bytes written
 */
static ssize_t hdd_injection_sysfs_debug_level_show(struct kobject *kobj,
						     struct kobj_attribute *attr,
						     char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", g_injection_debug_level);
}

/**
 * hdd_injection_sysfs_debug_level_store() - Set debug level via sysfs
 * @kobj: Kobject pointer
 * @attr: Attribute pointer
 * @buf: Buffer to read from
 * @count: Number of bytes to read
 *
 * Return: Number of bytes read, or error code
 */
static ssize_t hdd_injection_sysfs_debug_level_store(struct kobject *kobj,
						      struct kobj_attribute *attr,
						      const char *buf,
						      size_t count)
{
	uint8_t debug_level;
	int ret;

	ret = kstrtou8(buf, 10, &debug_level);
	if (ret) {
		return ret;
	}

	if (debug_level > HDD_INJECT_DEBUG_LEVEL_VERBOSE) {
		return -EINVAL;
	}

	g_injection_debug_level = debug_level;
	return count;
}

/**
 * hdd_injection_sysfs_global_enable_show() - Show global enable status via sysfs
 * @kobj: Kobject pointer
 * @attr: Attribute pointer
 * @buf: Buffer to write to
 *
 * Return: Number of bytes written
 */
static ssize_t hdd_injection_sysfs_global_enable_show(struct kobject *kobj,
						       struct kobj_attribute *attr,
						       char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", g_injection_global_enable ? 1 : 0);
}

/**
 * hdd_injection_sysfs_global_enable_store() - Set global enable status via sysfs
 * @kobj: Kobject pointer
 * @attr: Attribute pointer
 * @buf: Buffer to read from
 * @count: Number of bytes to read
 *
 * Return: Number of bytes read, or error code
 */
static ssize_t hdd_injection_sysfs_global_enable_store(struct kobject *kobj,
							struct kobj_attribute *attr,
							const char *buf,
							size_t count)
{
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret) {
		return ret;
	}

	g_injection_global_enable = enable;
	pr_info("Frame injection global enable set to: %s\n", enable ? "true" : "false");
	
	return count;
}

/**
 * Additional sysfs configuration functions
 */
static ssize_t hdd_injection_sysfs_max_frame_rate_show(struct kobject *kobj,
							struct kobj_attribute *attr,
							char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", g_injection_max_frame_rate);
}

static ssize_t hdd_injection_sysfs_max_frame_rate_store(struct kobject *kobj,
							 struct kobj_attribute *attr,
							 const char *buf,
							 size_t count)
{
	uint32_t rate;
	int ret;

	ret = kstrtou32(buf, 10, &rate);
	if (ret) {
		return ret;
	}

	if (rate > 10000) { /* Reasonable upper limit */
		return -EINVAL;
	}

	g_injection_max_frame_rate = rate;
	return count;
}

static ssize_t hdd_injection_sysfs_max_frame_size_show(struct kobject *kobj,
							struct kobj_attribute *attr,
							char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", g_injection_max_frame_size);
}

static ssize_t hdd_injection_sysfs_max_frame_size_store(struct kobject *kobj,
							 struct kobj_attribute *attr,
							 const char *buf,
							 size_t count)
{
	uint32_t size;
	int ret;

	ret = kstrtou32(buf, 10, &size);
	if (ret) {
		return ret;
	}

	if (size < 64 || size > 4096) { /* Reasonable bounds */
		return -EINVAL;
	}

	g_injection_max_frame_size = size;
	return count;
}

static ssize_t hdd_injection_sysfs_max_queue_size_show(struct kobject *kobj,
							struct kobj_attribute *attr,
							char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", g_injection_max_queue_size);
}

static ssize_t hdd_injection_sysfs_max_queue_size_store(struct kobject *kobj,
							 struct kobj_attribute *attr,
							 const char *buf,
							 size_t count)
{
	uint32_t size;
	int ret;

	ret = kstrtou32(buf, 10, &size);
	if (ret) {
		return ret;
	}

	if (size < 1 || size > 1024) { /* Reasonable bounds */
		return -EINVAL;
	}

	g_injection_max_queue_size = size;
	return count;
}

static ssize_t hdd_injection_sysfs_rate_window_ms_show(struct kobject *kobj,
							struct kobj_attribute *attr,
							char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", g_injection_rate_window_ms);
}

static ssize_t hdd_injection_sysfs_rate_window_ms_store(struct kobject *kobj,
							 struct kobj_attribute *attr,
							 const char *buf,
							 size_t count)
{
	uint32_t window;
	int ret;

	ret = kstrtou32(buf, 10, &window);
	if (ret) {
		return ret;
	}

	if (window < 100 || window > 60000) { /* 100ms to 60s */
		return -EINVAL;
	}

	g_injection_rate_window_ms = window;
	return count;
}

static ssize_t hdd_injection_sysfs_require_monitor_mode_show(struct kobject *kobj,
							     struct kobj_attribute *attr,
							     char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", g_injection_require_monitor_mode ? 1 : 0);
}

static ssize_t hdd_injection_sysfs_require_monitor_mode_store(struct kobject *kobj,
							      struct kobj_attribute *attr,
							      const char *buf,
							      size_t count)
{
	bool require;
	int ret;

	ret = kstrtobool(buf, &require);
	if (ret) {
		return ret;
	}

	g_injection_require_monitor_mode = require;
	return count;
}

/* Sysfs attributes */
static struct kobj_attribute hdd_injection_debug_level_attr =
	__ATTR(debug_level, 0644, hdd_injection_sysfs_debug_level_show,
	       hdd_injection_sysfs_debug_level_store);

static struct kobj_attribute hdd_injection_global_enable_attr =
	__ATTR(global_enable, 0644, hdd_injection_sysfs_global_enable_show,
	       hdd_injection_sysfs_global_enable_store);

static struct kobj_attribute hdd_injection_max_frame_rate_attr =
	__ATTR(max_frame_rate, 0644, hdd_injection_sysfs_max_frame_rate_show,
	       hdd_injection_sysfs_max_frame_rate_store);

static struct kobj_attribute hdd_injection_max_frame_size_attr =
	__ATTR(max_frame_size, 0644, hdd_injection_sysfs_max_frame_size_show,
	       hdd_injection_sysfs_max_frame_size_store);

static struct kobj_attribute hdd_injection_max_queue_size_attr =
	__ATTR(max_queue_size, 0644, hdd_injection_sysfs_max_queue_size_show,
	       hdd_injection_sysfs_max_queue_size_store);

static struct kobj_attribute hdd_injection_rate_window_ms_attr =
	__ATTR(rate_window_ms, 0644, hdd_injection_sysfs_rate_window_ms_show,
	       hdd_injection_sysfs_rate_window_ms_store);

static struct kobj_attribute hdd_injection_require_monitor_mode_attr =
	__ATTR(require_monitor_mode, 0644, hdd_injection_sysfs_require_monitor_mode_show,
	       hdd_injection_sysfs_require_monitor_mode_store);

static struct attribute *hdd_injection_sysfs_attrs[] = {
	&hdd_injection_debug_level_attr.attr,
	&hdd_injection_global_enable_attr.attr,
	&hdd_injection_max_frame_rate_attr.attr,
	&hdd_injection_max_frame_size_attr.attr,
	&hdd_injection_max_queue_size_attr.attr,
	&hdd_injection_rate_window_ms_attr.attr,
	&hdd_injection_require_monitor_mode_attr.attr,
	NULL,
};

static struct attribute_group hdd_injection_sysfs_attr_group = {
	.attrs = hdd_injection_sysfs_attrs,
};

/**
 * hdd_injection_create_debugfs_entries() - Create debugfs entries for adapter
 * @adapter: HDD adapter
 *
 * This function creates debugfs entries for frame injection debugging.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_create_debugfs_entries(struct hdd_adapter *adapter)
{
	struct dentry *adapter_dir;
	char dir_name[32];

	if (!adapter || !g_injection_debugfs_root) {
		return QDF_STATUS_E_INVAL;
	}

	/* Create adapter-specific directory */
	snprintf(dir_name, sizeof(dir_name), "%s", adapter->dev->name);
	adapter_dir = debugfs_create_dir(dir_name, g_injection_debugfs_root);
	if (IS_ERR_OR_NULL(adapter_dir)) {
		hdd_warn("Failed to create debugfs directory for %s", adapter->dev->name);
		return QDF_STATUS_E_FAILURE;
	}

	/* Create statistics file */
	debugfs_create_file("stats", 0444, adapter_dir, adapter,
			    &hdd_injection_debugfs_stats_fops);

	/* Create configuration file */
	debugfs_create_file("config", 0444, adapter_dir, adapter,
			    &hdd_injection_debugfs_config_fops);

	/* Create reset file */
	debugfs_create_file("reset", 0200, adapter_dir, adapter,
			    &hdd_injection_debugfs_reset_fops);

	/* Store directory pointer in adapter context for cleanup */
	if (adapter->injection_ctx) {
		adapter->injection_ctx->debugfs_dir = adapter_dir;
		hdd_info("Created debugfs entries for %s", adapter->dev->name);
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_injection_remove_debugfs_entries() - Remove debugfs entries for adapter
 * @adapter: HDD adapter
 *
 * This function removes debugfs entries for frame injection debugging.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_remove_debugfs_entries(struct hdd_adapter *adapter)
{
	if (!adapter) {
		return QDF_STATUS_E_INVAL;
	}

	/* Remove adapter-specific directory using stored pointer */
	if (adapter->injection_ctx && adapter->injection_ctx->debugfs_dir) {
		debugfs_remove_recursive(adapter->injection_ctx->debugfs_dir);
		adapter->injection_ctx->debugfs_dir = NULL;
		hdd_info("Removed debugfs entries for %s", adapter->dev->name);
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_injection_init_debug_interfaces() - Initialize debug interfaces
 *
 * This function initializes debugfs and sysfs interfaces for frame injection.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_init_debug_interfaces(void)
{
	int ret;

	/* Create debugfs root directory */
	g_injection_debugfs_root = debugfs_create_dir("frame_injection", NULL);
	if (IS_ERR_OR_NULL(g_injection_debugfs_root)) {
		hdd_warn("Failed to create frame injection debugfs root");
		g_injection_debugfs_root = NULL;
		/* Continue without debugfs - not critical */
	}

	/* Create sysfs kobject */
	g_injection_sysfs_kobj = kobject_create_and_add("frame_injection",
							 kernel_kobj);
	if (!g_injection_sysfs_kobj) {
		hdd_warn("Failed to create frame injection sysfs kobject");
		/* Continue without sysfs - not critical */
	} else {
		/* Create sysfs attribute group */
		ret = sysfs_create_group(g_injection_sysfs_kobj,
					 &hdd_injection_sysfs_attr_group);
		if (ret) {
			hdd_warn("Failed to create sysfs attribute group: %d", ret);
			kobject_put(g_injection_sysfs_kobj);
			g_injection_sysfs_kobj = NULL;
			/* Continue without sysfs - not critical */
		}
	}

	hdd_info("Frame injection debug interfaces initialized");
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_injection_deinit_debug_interfaces() - Deinitialize debug interfaces
 *
 * This function cleans up debugfs and sysfs interfaces for frame injection.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_deinit_debug_interfaces(void)
{
	/* Remove sysfs interfaces */
	if (g_injection_sysfs_kobj) {
		sysfs_remove_group(g_injection_sysfs_kobj,
				   &hdd_injection_sysfs_attr_group);
		kobject_put(g_injection_sysfs_kobj);
		g_injection_sysfs_kobj = NULL;
	}

	/* Remove debugfs interfaces */
	if (g_injection_debugfs_root) {
		debugfs_remove_recursive(g_injection_debugfs_root);
		g_injection_debugfs_root = NULL;
	}

	hdd_info("Frame injection debug interfaces deinitialized");
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_injection_log_with_level() - Log message with configurable level
 * @level: Log level
 * @fmt: Format string
 * @...: Variable arguments
 *
 * This function provides configurable debug logging for frame injection.
 */
void hdd_injection_log_with_level(uint8_t level, const char *fmt, ...)
{
	va_list args;
	char log_buf[256];

	if (level > g_injection_debug_level) {
		return;
	}

	va_start(args, fmt);
	vsnprintf(log_buf, sizeof(log_buf), fmt, args);
	va_end(args);

	switch (level) {
	case HDD_INJECT_DEBUG_LEVEL_ERROR:
		hdd_err("INJECT: %s", log_buf);
		break;
	case HDD_INJECT_DEBUG_LEVEL_WARN:
		hdd_warn("INJECT: %s", log_buf);
		break;
	case HDD_INJECT_DEBUG_LEVEL_INFO:
		hdd_info("INJECT: %s", log_buf);
		break;
	case HDD_INJECT_DEBUG_LEVEL_DEBUG:
		hdd_debug("INJECT: %s", log_buf);
		break;
	case HDD_INJECT_DEBUG_LEVEL_VERBOSE:
		QDF_TRACE(QDF_MODULE_ID_HDD, QDF_TRACE_LEVEL_DEBUG,
			  "INJECT: %s", log_buf);
		break;
	default:
		break;
	}
}

/**
 * hdd_injection_get_global_config() - Get global injection configuration
 * @config: Pointer to configuration structure to fill
 *
 * This function retrieves the current global configuration parameters
 * that can be modified via sysfs interface.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_get_global_config(struct injection_config *config)
{
	if (!config) {
		return QDF_STATUS_E_INVAL;
	}

	config->injection_enabled = g_injection_global_enable;
	config->max_frame_rate = g_injection_max_frame_rate;
	config->max_frame_size = g_injection_max_frame_size;
	config->max_queue_size = g_injection_max_queue_size;
	config->rate_window_ms = g_injection_rate_window_ms;
	config->require_monitor_mode = g_injection_require_monitor_mode;
	config->log_level = g_injection_debug_level;

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_injection_is_globally_enabled() - Check if injection is globally enabled
 *
 * This function checks the global enable flag that can be controlled
 * via sysfs interface.
 *
 * Return: true if globally enabled, false otherwise
 */
bool hdd_injection_is_globally_enabled(void)
{
	return g_injection_global_enable;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */