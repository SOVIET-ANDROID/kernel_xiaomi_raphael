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
 * DOC: wlan_hdd_frame_inject_integration.c
 *
 * WLAN Host Device Driver Frame Injection System Integration
 * This file implements the complete integration of frame injection
 * components from HDD to firmware, including interface mode coordination,
 * resource management, and end-to-end testing.
 */

#include "wlan_hdd_includes.h"
#include "wlan_hdd_frame_inject.h"
#include "wlan_hdd_frame_inject_integration.h"
#include "wma_frame_inject.h"
#include "wma_api.h"
#include "cds_api.h"
#include <qdf_mem.h>
#include <qdf_trace.h>

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/* Integration logging macros */
#define hdd_integration_debug(params...) \
	QDF_TRACE_DEBUG(QDF_MODULE_ID_HDD, params)
#define hdd_integration_info(params...) \
	QDF_TRACE_INFO(QDF_MODULE_ID_HDD, params)
#define hdd_integration_warn(params...) \
	QDF_TRACE_WARN(QDF_MODULE_ID_HDD, params)
#define hdd_integration_err(params...) \
	QDF_TRACE_ERROR(QDF_MODULE_ID_HDD, params)

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
QDF_STATUS hdd_wire_injection_components(struct hdd_context *hdd_ctx)
{
	tp_wma_handle wma_handle;
	QDF_STATUS status;
	struct hdd_adapter *adapter;
	int adapter_count = 0;

	hdd_integration_debug("Wiring injection components together");

	if (!hdd_ctx) {
		hdd_integration_err("Invalid HDD context");
		return QDF_STATUS_E_INVAL;
	}

	/* Get WMA handle */
	wma_handle = cds_get_context(QDF_MODULE_ID_WMA);
	if (!wma_handle) {
		hdd_integration_err("Failed to get WMA handle");
		return QDF_STATUS_E_FAILURE;
	}

	/* Initialize WMA injection queue first */
	status = wma_init_injection_queue(wma_handle);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_integration_err("Failed to initialize WMA injection queue: %d", status);
		return status;
	}

	/* Initialize injection for all existing adapters */
	hdd_for_each_adapter(hdd_ctx, adapter) {
		if (!adapter) {
			continue;
		}

		/* Initialize frame injection for this adapter */
		status = hdd_init_frame_injection(adapter);
		if (QDF_IS_STATUS_ERROR(status)) {
			hdd_integration_warn("Failed to initialize injection for adapter %d: %d",
					     adapter->vdev_id, status);
			continue;
		}

		/* Store WMA handle reference in adapter injection context */
		if (adapter->injection_ctx) {
			adapter->injection_ctx->wma_handle = wma_handle;
		}

		adapter_count++;
		hdd_integration_debug("Initialized injection for adapter %d (vdev_id=%d)",
				     adapter_count, adapter->vdev_id);
	}

	hdd_integration_info("Successfully wired injection components: %d adapters initialized",
			    adapter_count);

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_test_injection_interface_modes() - Test injection with different interface modes
 * @hdd_ctx: HDD context
 *
 * This function tests frame injection functionality with different interface
 * modes and configurations to ensure compatibility.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_test_injection_interface_modes(struct hdd_context *hdd_ctx)
{
	struct hdd_adapter *adapter;
	QDF_STATUS status;
	bool test_passed = true;
	int tests_run = 0;
	int tests_passed = 0;

	hdd_integration_debug("Testing injection with different interface modes");

	if (!hdd_ctx) {
		hdd_integration_err("Invalid HDD context");
		return QDF_STATUS_E_INVAL;
	}

	/* Test with each adapter type */
	hdd_for_each_adapter(hdd_ctx, adapter) {
		if (!adapter || !adapter->injection_ctx) {
			continue;
		}

		tests_run++;

		hdd_integration_debug("Testing injection on adapter type %d (vdev_id=%d)",
				     adapter->device_mode, adapter->vdev_id);

		switch (adapter->device_mode) {
		case QDF_MONITOR_MODE:
			/* Monitor mode should support injection */
			status = hdd_frame_inject_enable(adapter);
			if (QDF_IS_STATUS_SUCCESS(status)) {
				tests_passed++;
				hdd_integration_info("Monitor mode injection test PASSED");
			} else {
				test_passed = false;
				hdd_integration_err("Monitor mode injection test FAILED: %d", status);
			}
			break;

		case QDF_STA_MODE:
		case QDF_SAP_MODE:
		case QDF_P2P_CLIENT_MODE:
		case QDF_P2P_GO_MODE:
			/* These modes may have limited injection support */
			status = hdd_frame_inject_enable(adapter);
			if (QDF_IS_STATUS_SUCCESS(status) || status == QDF_STATUS_E_NOSUPPORT) {
				tests_passed++;
				hdd_integration_info("Mode %d injection test PASSED (status=%d)",
						    adapter->device_mode, status);
			} else {
				test_passed = false;
				hdd_integration_err("Mode %d injection test FAILED: %d",
						   adapter->device_mode, status);
			}
			break;

		default:
			hdd_integration_debug("Skipping unsupported mode %d", adapter->device_mode);
			tests_run--; /* Don't count this as a test */
			break;
		}

		/* Test disabling injection */
		status = hdd_frame_inject_disable(adapter);
		if (QDF_IS_STATUS_ERROR(status)) {
			test_passed = false;
			hdd_integration_err("Failed to disable injection on adapter %d: %d",
					   adapter->vdev_id, status);
		}
	}

	hdd_integration_info("Interface mode testing complete: %d/%d tests passed",
			    tests_passed, tests_run);

	return test_passed ? QDF_STATUS_SUCCESS : QDF_STATUS_E_FAILURE;
}

/**
 * hdd_verify_injection_cleanup() - Verify proper cleanup and resource management
 * @hdd_ctx: HDD context
 *
 * This function verifies that injection resources are properly cleaned up
 * when adapters are removed or the system shuts down.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_verify_injection_cleanup(struct hdd_context *hdd_ctx)
{
	struct hdd_adapter *adapter;
	tp_wma_handle wma_handle;
	QDF_STATUS status;
	bool cleanup_verified = true;
	int adapters_cleaned = 0;

	hdd_integration_debug("Verifying injection cleanup and resource management");

	if (!hdd_ctx) {
		hdd_integration_err("Invalid HDD context");
		return QDF_STATUS_E_INVAL;
	}

	wma_handle = cds_get_context(QDF_MODULE_ID_WMA);
	if (!wma_handle) {
		hdd_integration_err("Failed to get WMA handle");
		return QDF_STATUS_E_FAILURE;
	}

	/* Test cleanup for each adapter */
	hdd_for_each_adapter(hdd_ctx, adapter) {
		if (!adapter) {
			continue;
		}

		hdd_integration_debug("Testing cleanup for adapter %d (vdev_id=%d)",
				     adapters_cleaned, adapter->vdev_id);

		/* Verify injection context exists before cleanup */
		if (adapter->injection_ctx) {
			/* Test graceful cleanup */
			status = hdd_deinit_frame_injection(adapter);
			if (QDF_IS_STATUS_ERROR(status)) {
				cleanup_verified = false;
				hdd_integration_err("Failed to cleanup injection for adapter %d: %d",
						   adapter->vdev_id, status);
			} else {
				/* Verify context was properly cleaned up */
				if (adapter->injection_ctx != NULL) {
					cleanup_verified = false;
					hdd_integration_err("Injection context not properly cleared for adapter %d",
							   adapter->vdev_id);
				} else {
					hdd_integration_debug("Adapter %d cleanup verified", adapter->vdev_id);
				}
			}
		}

		adapters_cleaned++;
	}

	/* Test WMA queue cleanup */
	if (wma_is_injection_queue_empty(wma_handle)) {
		hdd_integration_debug("WMA injection queue is empty as expected");
	} else {
		hdd_integration_warn("WMA injection queue not empty, flushing");
		status = wma_flush_injection_queue(wma_handle);
		if (QDF_IS_STATUS_ERROR(status)) {
			cleanup_verified = false;
			hdd_integration_err("Failed to flush WMA injection queue: %d", status);
		}
	}

	/* Test WMA queue deinitialization */
	status = wma_deinit_injection_queue(wma_handle);
	if (QDF_IS_STATUS_ERROR(status)) {
		cleanup_verified = false;
		hdd_integration_err("Failed to deinitialize WMA injection queue: %d", status);
	}

	hdd_integration_info("Cleanup verification complete: %d adapters, result=%s",
			    adapters_cleaned, cleanup_verified ? "PASSED" : "FAILED");

	return cleanup_verified ? QDF_STATUS_SUCCESS : QDF_STATUS_E_FAILURE;
}

/**
 * hdd_test_injection_end_to_end() - Test complete injection flow end-to-end
 * @hdd_ctx: HDD context
 *
 * This function performs end-to-end testing of the injection system,
 * from userspace interface through to firmware transmission.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_test_injection_end_to_end(struct hdd_context *hdd_ctx)
{
	struct hdd_adapter *monitor_adapter = NULL;
	struct inject_frame_req *test_req = NULL;
	tp_wma_handle wma_handle;
	QDF_STATUS status;
	bool test_passed = true;
	uint8_t test_frame[] = {
		/* 802.11 Beacon frame */
		0x80, 0x00, 0x00, 0x00,             /* Frame Control + Flags */
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* Destination (broadcast) */
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* Source */
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* BSSID */
		0x00, 0x00,                         /* Sequence Control */
		/* Beacon frame body would follow */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Timestamp */
		0x64, 0x00,                         /* Beacon Interval */
		0x01, 0x04,                         /* Capability Info */
		0x00, 0x04, 'T', 'E', 'S', 'T'      /* SSID IE */
	};

	hdd_integration_debug("Starting end-to-end injection test");

	if (!hdd_ctx) {
		hdd_integration_err("Invalid HDD context");
		return QDF_STATUS_E_INVAL;
	}

	wma_handle = cds_get_context(QDF_MODULE_ID_WMA);
	if (!wma_handle) {
		hdd_integration_err("Failed to get WMA handle");
		return QDF_STATUS_E_FAILURE;
	}

	/* Find a monitor mode adapter for testing */
	monitor_adapter = hdd_get_adapter(hdd_ctx, QDF_MONITOR_MODE);
	if (!monitor_adapter) {
		hdd_integration_warn("No monitor adapter found, creating test adapter");
		/* In a real implementation, we might create a temporary adapter */
		/* For now, use any available adapter */
		monitor_adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
		if (!monitor_adapter) {
			hdd_integration_err("No suitable adapter found for testing");
			return QDF_STATUS_E_FAILURE;
		}
	}

	/* Ensure injection is initialized for the adapter */
	if (!monitor_adapter->injection_ctx) {
		status = hdd_init_frame_injection(monitor_adapter);
		if (QDF_IS_STATUS_ERROR(status)) {
			hdd_integration_err("Failed to initialize injection for test: %d", status);
			return status;
		}
	}

	/* Enable injection for the adapter */
	status = hdd_frame_inject_enable(monitor_adapter);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_integration_err("Failed to enable injection for test: %d", status);
		test_passed = false;
		goto cleanup;
	}

	/* Create test injection request */
	test_req = qdf_mem_malloc(sizeof(*test_req));
	if (!test_req) {
		hdd_integration_err("Failed to allocate test injection request");
		test_passed = false;
		goto cleanup;
	}

	/* Allocate and copy test frame data */
	test_req->frame_data = qdf_mem_malloc(sizeof(test_frame));
	if (!test_req->frame_data) {
		hdd_integration_err("Failed to allocate test frame data");
		test_passed = false;
		goto cleanup;
	}

	qdf_mem_copy(test_req->frame_data, test_frame, sizeof(test_frame));
	test_req->frame_len = sizeof(test_frame);
	test_req->tx_flags = 0;
	test_req->retry_count = 0;
	test_req->tx_rate = 0;
	test_req->timestamp = qdf_get_log_timestamp();
	test_req->session_id = 12345; /* Test session ID */

	hdd_integration_info("Testing HDD layer processing");

	/* Test HDD layer processing */
	status = hdd_process_frame_injection(monitor_adapter, test_req);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_integration_err("HDD layer processing failed: %d", status);
		test_passed = false;
		goto cleanup;
	}

	hdd_integration_info("HDD layer processing PASSED");

	/* Wait for queue processing */
	qdf_sleep(100); /* 100ms to allow queue processing */

	/* Test WMA layer queue status */
	if (!wma_is_injection_queue_empty(wma_handle)) {
		hdd_integration_info("WMA queue has pending frames (expected)");
	}

	/* Test WMA queue processing */
	status = wma_process_injection_queue(wma_handle);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_integration_err("WMA queue processing failed: %d", status);
		test_passed = false;
		goto cleanup;
	}

	hdd_integration_info("WMA layer processing PASSED");

	/* Test direct firmware interface */
	status = wma_send_injection_frame_to_fw(wma_handle, test_req, monitor_adapter->vdev_id);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_integration_err("Firmware interface test failed: %d", status);
		test_passed = false;
		goto cleanup;
	}

	hdd_integration_info("Firmware interface test PASSED");

cleanup:
	/* Cleanup test resources */
	if (test_req) {
		if (test_req->frame_data) {
			qdf_mem_free(test_req->frame_data);
		}
		qdf_mem_free(test_req);
	}

	/* Disable injection */
	if (monitor_adapter) {
		hdd_frame_inject_disable(monitor_adapter);
	}

	hdd_integration_info("End-to-end injection test %s",
			    test_passed ? "PASSED" : "FAILED");

	return test_passed ? QDF_STATUS_SUCCESS : QDF_STATUS_E_FAILURE;
}

/**
 * hdd_integration_test_suite() - Run complete integration test suite
 * @hdd_ctx: HDD context
 *
 * This function runs the complete integration test suite for frame injection,
 * covering component wiring, interface modes, cleanup, and end-to-end flow.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code on failure
 */
QDF_STATUS hdd_integration_test_suite(struct hdd_context *hdd_ctx)
{
	QDF_STATUS status;
	bool all_tests_passed = true;
	int tests_run = 0;
	int tests_passed = 0;

	hdd_integration_info("Starting frame injection integration test suite");

	if (!hdd_ctx) {
		hdd_integration_err("Invalid HDD context");
		return QDF_STATUS_E_INVAL;
	}

	/* Test 1: Component wiring */
	hdd_integration_info("=== Test 1: Component Wiring ===");
	tests_run++;
	status = hdd_wire_injection_components(hdd_ctx);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		tests_passed++;
		hdd_integration_info("Component wiring test PASSED");
	} else {
		all_tests_passed = false;
		hdd_integration_err("Component wiring test FAILED: %d", status);
	}

	/* Test 2: Interface mode compatibility */
	hdd_integration_info("=== Test 2: Interface Mode Compatibility ===");
	tests_run++;
	status = hdd_test_injection_interface_modes(hdd_ctx);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		tests_passed++;
		hdd_integration_info("Interface mode test PASSED");
	} else {
		all_tests_passed = false;
		hdd_integration_err("Interface mode test FAILED: %d", status);
	}

	/* Test 3: End-to-end flow */
	hdd_integration_info("=== Test 3: End-to-End Flow ===");
	tests_run++;
	status = hdd_test_injection_end_to_end(hdd_ctx);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		tests_passed++;
		hdd_integration_info("End-to-end test PASSED");
	} else {
		all_tests_passed = false;
		hdd_integration_err("End-to-end test FAILED: %d", status);
	}

	/* Test 4: Cleanup and resource management */
	hdd_integration_info("=== Test 4: Cleanup and Resource Management ===");
	tests_run++;
	status = hdd_verify_injection_cleanup(hdd_ctx);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		tests_passed++;
		hdd_integration_info("Cleanup test PASSED");
	} else {
		all_tests_passed = false;
		hdd_integration_err("Cleanup test FAILED: %d", status);
	}

	/* Test suite summary */
	hdd_integration_info("=== Integration Test Suite Summary ===");
	hdd_integration_info("Tests run: %d", tests_run);
	hdd_integration_info("Tests passed: %d", tests_passed);
	hdd_integration_info("Tests failed: %d", tests_run - tests_passed);
	hdd_integration_info("Overall result: %s", all_tests_passed ? "PASSED" : "FAILED");

	return all_tests_passed ? QDF_STATUS_SUCCESS : QDF_STATUS_E_FAILURE;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */