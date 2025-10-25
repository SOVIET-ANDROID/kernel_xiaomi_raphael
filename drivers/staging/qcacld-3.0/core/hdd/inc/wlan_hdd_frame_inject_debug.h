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

#ifndef __WLAN_HDD_FRAME_INJECT_DEBUG_H
#define __WLAN_HDD_FRAME_INJECT_DEBUG_H

/**
 * DOC: wlan_hdd_frame_inject_debug.h
 *
 * WLAN Host Device Driver Frame Injection Debug and Diagnostic APIs
 */

#include <qdf_types.h>
#include <qdf_status.h>

/* Forward declarations */
struct hdd_adapter;
struct injection_config;

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/**
 * hdd_injection_create_debugfs_entries() - Create debugfs entries for adapter
 * @adapter: HDD adapter
 *
 * This function creates debugfs entries for frame injection debugging.
 * It creates per-adapter directories with statistics, configuration,
 * and control files.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_create_debugfs_entries(struct hdd_adapter *adapter);

/**
 * hdd_injection_remove_debugfs_entries() - Remove debugfs entries for adapter
 * @adapter: HDD adapter
 *
 * This function removes debugfs entries for frame injection debugging.
 * It cleans up all files and directories created for the adapter.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_remove_debugfs_entries(struct hdd_adapter *adapter);

/**
 * hdd_injection_init_debug_interfaces() - Initialize debug interfaces
 *
 * This function initializes debugfs and sysfs interfaces for frame injection.
 * It creates the root debugfs directory and sysfs kobject for global
 * configuration and control.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_init_debug_interfaces(void);

/**
 * hdd_injection_deinit_debug_interfaces() - Deinitialize debug interfaces
 *
 * This function cleans up debugfs and sysfs interfaces for frame injection.
 * It removes all global debug interfaces and frees associated resources.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_deinit_debug_interfaces(void);

/**
 * hdd_injection_log_with_level() - Log message with configurable level
 * @level: Log level (0=none, 1=error, 2=warn, 3=info, 4=debug, 5=verbose)
 * @fmt: Format string
 * @...: Variable arguments
 *
 * This function provides configurable debug logging for frame injection.
 * The log level can be controlled via sysfs interface.
 */
void hdd_injection_log_with_level(uint8_t level, const char *fmt, ...);

/**
 * hdd_injection_get_global_config() - Get global injection configuration
 * @config: Pointer to configuration structure to fill
 *
 * This function retrieves the current global configuration parameters
 * that can be modified via sysfs interface.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_get_global_config(struct injection_config *config);

/**
 * hdd_injection_is_globally_enabled() - Check if injection is globally enabled
 *
 * This function checks the global enable flag that can be controlled
 * via sysfs interface.
 *
 * Return: true if globally enabled, false otherwise
 */
bool hdd_injection_is_globally_enabled(void);

/* Convenience macros for different log levels */
#define hdd_inject_log_error(fmt, args...) \
	hdd_injection_log_with_level(1, fmt, ##args)

#define hdd_inject_log_warn(fmt, args...) \
	hdd_injection_log_with_level(2, fmt, ##args)

#define hdd_inject_log_info(fmt, args...) \
	hdd_injection_log_with_level(3, fmt, ##args)

#define hdd_inject_log_debug(fmt, args...) \
	hdd_injection_log_with_level(4, fmt, ##args)

#define hdd_inject_log_verbose(fmt, args...) \
	hdd_injection_log_with_level(5, fmt, ##args)

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline QDF_STATUS hdd_injection_create_debugfs_entries(struct hdd_adapter *adapter)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS hdd_injection_remove_debugfs_entries(struct hdd_adapter *adapter)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS hdd_injection_init_debug_interfaces(void)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS hdd_injection_deinit_debug_interfaces(void)
{
	return QDF_STATUS_SUCCESS;
}

static inline void hdd_injection_log_with_level(uint8_t level, const char *fmt, ...)
{
	/* No-op when feature is disabled */
}

static inline QDF_STATUS hdd_injection_get_global_config(struct injection_config *config)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline bool hdd_injection_is_globally_enabled(void)
{
	return false;
}

#define hdd_inject_log_error(fmt, args...)   do { } while (0)
#define hdd_inject_log_warn(fmt, args...)    do { } while (0)
#define hdd_inject_log_info(fmt, args...)    do { } while (0)
#define hdd_inject_log_debug(fmt, args...)   do { } while (0)
#define hdd_inject_log_verbose(fmt, args...) do { } while (0)

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WLAN_HDD_FRAME_INJECT_DEBUG_H */