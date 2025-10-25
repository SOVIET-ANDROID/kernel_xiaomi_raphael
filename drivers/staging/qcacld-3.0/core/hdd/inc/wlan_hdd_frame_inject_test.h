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

#ifndef __WLAN_HDD_FRAME_INJECT_TEST_H
#define __WLAN_HDD_FRAME_INJECT_TEST_H

/**
 * DOC: wlan_hdd_frame_inject_test.h
 *
 * WLAN Host Device Driver Frame Injection Integration Test APIs
 */

#include <qdf_types.h>
#include <qdf_status.h>

/* Forward declarations */
struct hdd_adapter;

/**
 * struct hdd_injection_test_stats - Test statistics structure
 * @tests_run: Number of tests executed
 * @tests_passed: Number of tests that passed
 * @tests_failed: Number of tests that failed
 * @assertions_checked: Number of assertions checked
 * @assertions_failed: Number of assertions that failed
 */
struct hdd_injection_test_stats {
	uint32_t tests_run;
	uint32_t tests_passed;
	uint32_t tests_failed;
	uint32_t assertions_checked;
	uint32_t assertions_failed;
};

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/**
 * hdd_injection_run_integration_tests() - Run all integration tests
 * @adapter: HDD adapter to test with
 *
 * This function runs a comprehensive suite of integration tests for
 * frame injection functionality. It tests:
 * - Basic initialization and cleanup
 * - Frame validation and processing
 * - Error handling and recovery mechanisms
 * - Concurrent operations and queue management
 * - Debug interfaces and logging
 *
 * The tests are designed to verify end-to-end functionality and
 * ensure that error conditions are handled gracefully.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code otherwise
 */
QDF_STATUS hdd_injection_run_integration_tests(struct hdd_adapter *adapter);

/**
 * hdd_injection_get_test_stats() - Get test statistics
 * @stats: Output test statistics structure
 *
 * This function retrieves the current test statistics including
 * number of tests run, passed, failed, and assertion results.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_injection_get_test_stats(struct hdd_injection_test_stats *stats);

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline QDF_STATUS hdd_injection_run_integration_tests(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_injection_get_test_stats(struct hdd_injection_test_stats *stats)
{
	return QDF_STATUS_E_NOSUPPORT;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WLAN_HDD_FRAME_INJECT_TEST_H */