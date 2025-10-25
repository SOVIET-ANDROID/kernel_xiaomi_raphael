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

#ifndef __WLAN_HDD_FRAME_INJECT_INTEGRATION_H
#define __WLAN_HDD_FRAME_INJECT_INTEGRATION_H

/**
 * DOC: wlan_hdd_frame_inject_integration.h
 *
 * WLAN Host Device Driver Frame Injection System Integration APIs
 */

#include <qdf_types.h>
#include <qdf_status.h>

/* Forward declarations */
struct hdd_context;
struct hdd_adapter;

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/**
 * hdd_wire_injection_components() - Wire together all injection components
 * @hdd_ctx: HDD context
 *
 * This function establishes the complete integration between HDD layer
 * frame injection, WMA layer queue management, and firmware interface.
 * It ensures all components are properly initialized and connected.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_wire_injection_components(struct hdd_context *hdd_ctx);

/**
 * hdd_test_injection_interface_modes() - Test injection with different interface modes
 * @hdd_ctx: HDD context
 *
 * This function tests frame injection functionality with different interface
 * modes and configurations to ensure compatibility.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_test_injection_interface_modes(struct hdd_context *hdd_ctx);

/**
 * hdd_verify_injection_cleanup() - Verify proper cleanup and resource management
 * @hdd_ctx: HDD context
 *
 * This function verifies that injection resources are properly cleaned up
 * when adapters are removed or the system shuts down.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_verify_injection_cleanup(struct hdd_context *hdd_ctx);

/**
 * hdd_test_injection_end_to_end() - Test complete injection flow end-to-end
 * @hdd_ctx: HDD context
 *
 * This function performs end-to-end testing of the injection system,
 * from userspace interface through to firmware transmission.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_test_injection_end_to_end(struct hdd_context *hdd_ctx);

/**
 * hdd_integration_test_suite() - Run complete integration test suite
 * @hdd_ctx: HDD context
 *
 * This function runs the complete integration test suite for frame injection,
 * covering component wiring, interface modes, cleanup, and end-to-end flow.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code on failure
 */
QDF_STATUS hdd_integration_test_suite(struct hdd_context *hdd_ctx);

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline QDF_STATUS hdd_wire_injection_components(struct hdd_context *hdd_ctx)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS hdd_test_injection_interface_modes(struct hdd_context *hdd_ctx)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_verify_injection_cleanup(struct hdd_context *hdd_ctx)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS hdd_test_injection_end_to_end(struct hdd_context *hdd_ctx)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_integration_test_suite(struct hdd_context *hdd_ctx)
{
	return QDF_STATUS_E_NOSUPPORT;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WLAN_HDD_FRAME_INJECT_INTEGRATION_H */