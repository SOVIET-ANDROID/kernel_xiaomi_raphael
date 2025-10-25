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
 * DOC: wlan_hdd_frame_inject_comprehensive_test.c
 *
 * WLAN Host Device Driver Frame Injection Comprehensive Testing Suite
 * This file implements comprehensive testing including unit tests, integration tests,
 * performance testing under various load conditions, and stability testing during
 * extended injection operations.
 */

#include "wlan_hdd_includes.h"
#include "wlan_hdd_frame_inject.h"
#include "wlan_hdd_frame_inject_test.h"
#include "wlan_hdd_frame_inject_security_test.h"
#include "wlan_hdd_frame_inject_integration.h"
#include "wlan_hdd_frame_inject_comprehensive_test.h"
#include "wma_frame_inject.h"
#include <qdf_mem.h>
#include <qdf_trace.h>
#include <qdf_timer.h>

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/* Comprehensive test logging macros */
#define hdd_comp_test_debug(params...) \
	QDF_TRACE_DEBUG(QDF_MODULE_ID_HDD, params)
#define hdd_comp_test_info(params...) \
	QDF_TRACE_INFO(QDF_MODULE_ID_HDD, params)
#define hdd_comp_test_warn(params...) \
	QDF_TRACE_WARN(QDF_MODULE_ID_HDD, params)
#define hdd_comp_test_err(params...) \
	QDF_TRACE_ERROR(QDF_MODULE_ID_HDD, params)

/**
 * struct hdd_performance_test_stats - Performance test statistics
 * @frames_sent: Total frames sent during test
 * @frames_successful: Frames successfully processed
 * @frames_failed: Frames that failed processing
 * @total_latency_us: Total latency in microseconds
 * @min_latency_us: Minimum latency observed
 * @max_latency_us: Maximum latency observed
 * @test_duration_ms: Total test duration in milliseconds
 * @throughput_fps: Achieved throughput in frames per second
 * @memory_peak_kb: Peak memory usage in KB
 * @cpu_usage_percent: Average CPU usage percentage
 */
struct hdd_performance_test_stats {
	uint32_t frames_sent;
	uint32_t frames_successful;
	uint32_t frames_failed;
	uint64_t total_latency_us;
	uint32_t min_latency_us;
	uint32_t max_latency_us;
	uint32_t test_duration_ms;
	uint32_t throughput_fps;
	uint32_t memory_peak_kb;
	uint32_t cpu_usage_percent;
};

/**
 * hdd_execute_unit_test_suite() - Execute full unit test suite
 * @adapter: HDD adapter
 *
 * This function executes all unit tests for frame injection components.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code on failure
 */
static QDF_STATUS hdd_execute_unit_test_suite(struct hdd_adapter *adapter)
{
	QDF_STATUS status;
	bool all_tests_passed = true;
	int test_suites_run = 0;
	int test_suites_passed = 0;

	hdd_comp_test_info("Executing unit test suite");

	/* Unit Test Suite 1: Basic functionality tests */
	hdd_comp_test_info("=== Unit Test Suite 1: Basic Functionality ===");
	test_suites_run++;
	status = hdd_injection_test_basic_functionality(adapter);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		test_suites_passed++;
		hdd_comp_test_info("Basic functionality unit tests PASSED");
	} else {
		all_tests_passed = false;
		hdd_comp_test_err("Basic functionality unit tests FAILED: %d", status);
	}

	/* Unit Test Suite 2: Frame validation tests */
	hdd_comp_test_info("=== Unit Test Suite 2: Frame Validation ===");
	test_suites_run++;
	status = hdd_injection_test_frame_validation(adapter);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		test_suites_passed++;
		hdd_comp_test_info("Frame validation unit tests PASSED");
	} else {
		all_tests_passed = false;
		hdd_comp_test_err("Frame validation unit tests FAILED: %d", status);
	}

	/* Unit Test Suite 3: Error handling tests */
	hdd_comp_test_info("=== Unit Test Suite 3: Error Handling ===");
	test_suites_run++;
	status = hdd_injection_test_error_handling(adapter);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		test_suites_passed++;
		hdd_comp_test_info("Error handling unit tests PASSED");
	} else {
		all_tests_passed = false;
		hdd_comp_test_err("Error handling unit tests FAILED: %d", status);
	}

	hdd_comp_test_info("Unit test suite complete: %d/%d suites passed",
			  test_suites_passed, test_suites_run);

	return all_tests_passed ? QDF_STATUS_SUCCESS : QDF_STATUS_E_FAILURE;
}

/**
 * hdd_execute_integration_test_suite() - Execute full integration test suite
 * @hdd_ctx: HDD context
 *
 * This function executes all integration tests for frame injection system.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code on failure
 */
static QDF_STATUS hdd_execute_integration_test_suite(struct hdd_context *hdd_ctx)
{
	QDF_STATUS status;

	hdd_comp_test_info("Executing integration test suite");

	/* Execute the integration test suite */
	status = hdd_integration_test_suite(hdd_ctx);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		hdd_comp_test_info("Integration test suite PASSED");
	} else {
		hdd_comp_test_err("Integration test suite FAILED: %d", status);
	}

	return status;
}

/**
 * hdd_test_performance_under_load() - Test performance under various load conditions
 * @adapter: HDD adapter
 * @load_type: Type of load to simulate
 * @stats: Performance statistics output
 *
 * This function tests injection performance under different load conditions.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_test_performance_under_load(struct hdd_adapter *adapter,
						  uint32_t load_type,
						  struct hdd_performance_test_stats *stats)
{
	struct inject_frame_req *test_req = NULL;
	QDF_STATUS status;
	uint64_t start_time, end_time, frame_start_time, frame_end_time;
	uint32_t i;
	uint32_t frames_to_send;
	uint32_t frame_size;
	uint32_t send_interval_us;
	bool test_passed = true;

	hdd_comp_test_info("Testing performance under load type %u", load_type);

	if (!adapter || !stats) {
		hdd_comp_test_err("Invalid parameters");
		return QDF_STATUS_E_INVAL;
	}

	/* Initialize statistics */
	qdf_mem_zero(stats, sizeof(*stats));
	stats->min_latency_us = UINT32_MAX;

	/* Configure test parameters based on load type */
	switch (load_type) {
	case 1: /* Light load */
		frames_to_send = 50;
		frame_size = 64;
		send_interval_us = 100000; /* 100ms between frames */
		break;
	case 2: /* Medium load */
		frames_to_send = 200;
		frame_size = 512;
		send_interval_us = 50000; /* 50ms between frames */
		break;
	case 3: /* Heavy load */
		frames_to_send = 500;
		frame_size = 1024;
		send_interval_us = 10000; /* 10ms between frames */
		break;
	case 4: /* Burst load */
		frames_to_send = 100;
		frame_size = 256;
		send_interval_us = 1000; /* 1ms between frames */
		break;
	default:
		hdd_comp_test_err("Invalid load type: %u", load_type);
		return QDF_STATUS_E_INVAL;
	}

	hdd_comp_test_info("Load test config: %u frames, %u bytes each, %u us interval",
			  frames_to_send, frame_size, send_interval_us);

	/* Allocate test frame data */
	uint8_t *frame_data = qdf_mem_malloc(frame_size);
	if (!frame_data) {
		hdd_comp_test_err("Failed to allocate test frame data");
		return QDF_STATUS_E_NOMEM;
	}

	/* Fill with test pattern */
	for (i = 0; i < frame_size; i++) {
		frame_data[i] = (uint8_t)(i % 256);
	}

	start_time = qdf_get_log_timestamp();

	/* Send frames and measure performance */
	for (i = 0; i < frames_to_send; i++) {
		/* Allocate injection request */
		test_req = qdf_mem_malloc(sizeof(*test_req));
		if (!test_req) {
			hdd_comp_test_err("Failed to allocate injection request %u", i);
			stats->frames_failed++;
			continue;
		}

		/* Allocate frame data copy */
		test_req->frame_data = qdf_mem_malloc(frame_size);
		if (!test_req->frame_data) {
			hdd_comp_test_err("Failed to allocate frame data for request %u", i);
			qdf_mem_free(test_req);
			stats->frames_failed++;
			continue;
		}

		qdf_mem_copy(test_req->frame_data, frame_data, frame_size);
		test_req->frame_len = frame_size;
		test_req->tx_flags = 0;
		test_req->retry_count = 0;
		test_req->tx_rate = 0;
		test_req->session_id = 60000 + i;

		frame_start_time = qdf_get_log_timestamp();
		test_req->timestamp = frame_start_time;

		/* Process injection request */
		status = hdd_process_frame_injection(adapter, test_req);

		frame_end_time = qdf_get_log_timestamp();

		stats->frames_sent++;

		if (QDF_IS_STATUS_SUCCESS(status)) {
			stats->frames_successful++;

			/* Calculate latency */
			uint32_t latency_us = (uint32_t)(frame_end_time - frame_start_time);
			stats->total_latency_us += latency_us;

			if (latency_us < stats->min_latency_us) {
				stats->min_latency_us = latency_us;
			}
			if (latency_us > stats->max_latency_us) {
				stats->max_latency_us = latency_us;
			}
		} else {
			stats->frames_failed++;
			hdd_comp_test_warn("Frame %u injection failed: %d", i, status);

			/* Cleanup on failure */
			if (test_req->frame_data) {
				qdf_mem_free(test_req->frame_data);
			}
			qdf_mem_free(test_req);
		}

		/* Inter-frame delay */
		if (send_interval_us > 0 && i < frames_to_send - 1) {
			qdf_udelay(send_interval_us);
		}
	}

	end_time = qdf_get_log_timestamp();

	/* Calculate final statistics */
	stats->test_duration_ms = (uint32_t)((end_time - start_time) / 1000);
	if (stats->test_duration_ms > 0) {
		stats->throughput_fps = (stats->frames_successful * 1000) / stats->test_duration_ms;
	}

	if (stats->frames_successful > 0) {
		stats->total_latency_us /= stats->frames_successful; /* Average latency */
	}

	/* Simulate memory and CPU usage (in a real implementation, these would be measured) */
	stats->memory_peak_kb = frame_size * frames_to_send / 1024;
	stats->cpu_usage_percent = (load_type * 15) % 100; /* Simulated CPU usage */

	/* Cleanup */
	qdf_mem_free(frame_data);

	/* Evaluate test results */
	uint32_t success_rate = (stats->frames_successful * 100) / stats->frames_sent;
	if (success_rate < 90) { /* Require 90% success rate */
		test_passed = false;
		hdd_comp_test_err("Performance test failed: success rate %u%% < 90%%", success_rate);
	}

	if (stats->throughput_fps < (frames_to_send / 10)) { /* Minimum expected throughput */
		test_passed = false;
		hdd_comp_test_err("Performance test failed: throughput %u fps too low", stats->throughput_fps);
	}

	hdd_comp_test_info("Performance test results:");
	hdd_comp_test_info("  Frames sent: %u", stats->frames_sent);
	hdd_comp_test_info("  Frames successful: %u", stats->frames_successful);
	hdd_comp_test_info("  Frames failed: %u", stats->frames_failed);
	hdd_comp_test_info("  Success rate: %u%%", success_rate);
	hdd_comp_test_info("  Average latency: %llu us", stats->total_latency_us);
	hdd_comp_test_info("  Min latency: %u us", stats->min_latency_us);
	hdd_comp_test_info("  Max latency: %u us", stats->max_latency_us);
	hdd_comp_test_info("  Test duration: %u ms", stats->test_duration_ms);
	hdd_comp_test_info("  Throughput: %u fps", stats->throughput_fps);

	return test_passed ? QDF_STATUS_SUCCESS : QDF_STATUS_E_FAILURE;
}

/**
 * hdd_test_stability_extended_operation() - Test stability during extended injection operations
 * @adapter: HDD adapter
 * @duration_minutes: Test duration in minutes
 *
 * This function tests system stability during extended injection operations.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_test_stability_extended_operation(struct hdd_adapter *adapter,
						       uint32_t duration_minutes)
{
	struct inject_frame_req *test_req = NULL;
	QDF_STATUS status;
	uint64_t start_time, current_time, last_stats_time;
	uint32_t total_frames_sent = 0;
	uint32_t total_frames_successful = 0;
	uint32_t total_frames_failed = 0;
	uint32_t consecutive_failures = 0;
	uint32_t max_consecutive_failures = 0;
	uint32_t stats_interval_ms = 30000; /* Print stats every 30 seconds */
	uint32_t frame_interval_ms = 100; /* Send frame every 100ms */
	uint32_t frame_size = 128;
	bool test_passed = true;
	bool test_running = true;

	hdd_comp_test_info("Starting stability test for %u minutes", duration_minutes);

	if (!adapter) {
		hdd_comp_test_err("Invalid adapter");
		return QDF_STATUS_E_INVAL;
	}

	/* Allocate test frame data */
	uint8_t *frame_data = qdf_mem_malloc(frame_size);
	if (!frame_data) {
		hdd_comp_test_err("Failed to allocate test frame data");
		return QDF_STATUS_E_NOMEM;
	}

	/* Fill with test pattern */
	qdf_mem_set(frame_data, frame_size, 0xDD);

	start_time = qdf_get_log_timestamp();
	last_stats_time = start_time;
	uint64_t test_duration_us = (uint64_t)duration_minutes * 60 * 1000000; /* Convert to microseconds */

	hdd_comp_test_info("Extended stability test running for %llu seconds...", test_duration_us / 1000000);

	while (test_running) {
		current_time = qdf_get_log_timestamp();

		/* Check if test duration has elapsed */
		if ((current_time - start_time) >= test_duration_us) {
			test_running = false;
			break;
		}

		/* Allocate injection request */
		test_req = qdf_mem_malloc(sizeof(*test_req));
		if (!test_req) {
			hdd_comp_test_warn("Failed to allocate injection request at frame %u", total_frames_sent);
			total_frames_failed++;
			consecutive_failures++;
			goto next_iteration;
		}

		/* Allocate frame data copy */
		test_req->frame_data = qdf_mem_malloc(frame_size);
		if (!test_req->frame_data) {
			hdd_comp_test_warn("Failed to allocate frame data at frame %u", total_frames_sent);
			qdf_mem_free(test_req);
			total_frames_failed++;
			consecutive_failures++;
			goto next_iteration;
		}

		qdf_mem_copy(test_req->frame_data, frame_data, frame_size);
		test_req->frame_len = frame_size;
		test_req->tx_flags = 0;
		test_req->retry_count = 0;
		test_req->tx_rate = 0;
		test_req->timestamp = current_time;
		test_req->session_id = 70000 + total_frames_sent;

		/* Process injection request */
		status = hdd_process_frame_injection(adapter, test_req);

		total_frames_sent++;

		if (QDF_IS_STATUS_SUCCESS(status)) {
			total_frames_successful++;
			consecutive_failures = 0;
		} else {
			total_frames_failed++;
			consecutive_failures++;

			/* Track maximum consecutive failures */
			if (consecutive_failures > max_consecutive_failures) {
				max_consecutive_failures = consecutive_failures;
			}

			/* Cleanup on failure */
			if (test_req->frame_data) {
				qdf_mem_free(test_req->frame_data);
			}
			qdf_mem_free(test_req);

			/* Check for excessive consecutive failures */
			if (consecutive_failures > 50) {
				hdd_comp_test_err("Too many consecutive failures (%u), aborting stability test",
						 consecutive_failures);
				test_passed = false;
				test_running = false;
				break;
			}
		}

next_iteration:
		/* Print periodic statistics */
		if ((current_time - last_stats_time) >= (stats_interval_ms * 1000)) {
			uint32_t elapsed_seconds = (uint32_t)((current_time - start_time) / 1000000);
			uint32_t success_rate = total_frames_sent > 0 ? 
				(total_frames_successful * 100) / total_frames_sent : 0;

			hdd_comp_test_info("Stability test progress (%u seconds elapsed):", elapsed_seconds);
			hdd_comp_test_info("  Total frames: %u", total_frames_sent);
			hdd_comp_test_info("  Successful: %u", total_frames_successful);
			hdd_comp_test_info("  Failed: %u", total_frames_failed);
			hdd_comp_test_info("  Success rate: %u%%", success_rate);
			hdd_comp_test_info("  Consecutive failures: %u", consecutive_failures);
			hdd_comp_test_info("  Max consecutive failures: %u", max_consecutive_failures);

			last_stats_time = current_time;
		}

		/* Inter-frame delay */
		qdf_sleep(frame_interval_ms);
	}

	/* Final statistics and evaluation */
	uint64_t total_duration_seconds = (current_time - start_time) / 1000000;
	uint32_t final_success_rate = total_frames_sent > 0 ? 
		(total_frames_successful * 100) / total_frames_sent : 0;

	hdd_comp_test_info("Extended stability test completed:");
	hdd_comp_test_info("  Duration: %llu seconds", total_duration_seconds);
	hdd_comp_test_info("  Total frames sent: %u", total_frames_sent);
	hdd_comp_test_info("  Successful frames: %u", total_frames_successful);
	hdd_comp_test_info("  Failed frames: %u", total_frames_failed);
	hdd_comp_test_info("  Final success rate: %u%%", final_success_rate);
	hdd_comp_test_info("  Maximum consecutive failures: %u", max_consecutive_failures);

	/* Evaluate stability criteria */
	if (final_success_rate < 95) { /* Require 95% success rate for stability */
		test_passed = false;
		hdd_comp_test_err("Stability test failed: success rate %u%% < 95%%", final_success_rate);
	}

	if (max_consecutive_failures > 20) { /* Allow max 20 consecutive failures */
		test_passed = false;
		hdd_comp_test_err("Stability test failed: max consecutive failures %u > 20", max_consecutive_failures);
	}

	/* Cleanup */
	qdf_mem_free(frame_data);

	hdd_comp_test_info("Extended stability test %s", test_passed ? "PASSED" : "FAILED");

	return test_passed ? QDF_STATUS_SUCCESS : QDF_STATUS_E_FAILURE;
}

/**
 * hdd_comprehensive_test_suite() - Execute complete comprehensive test suite
 * @hdd_ctx: HDD context
 *
 * This function executes the complete comprehensive test suite including
 * unit tests, integration tests, performance tests, and stability tests.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code on failure
 */
QDF_STATUS hdd_comprehensive_test_suite(struct hdd_context *hdd_ctx)
{
	struct hdd_adapter *test_adapter = NULL;
	QDF_STATUS status;
	bool all_tests_passed = true;
	int major_test_suites_run = 0;
	int major_test_suites_passed = 0;
	struct hdd_performance_test_stats perf_stats;

	hdd_comp_test_info("Starting comprehensive frame injection test suite");

	if (!hdd_ctx) {
		hdd_comp_test_err("Invalid HDD context");
		return QDF_STATUS_E_INVAL;
	}

	/* Find a suitable adapter for testing */
	test_adapter = hdd_get_adapter(hdd_ctx, QDF_MONITOR_MODE);
	if (!test_adapter) {
		test_adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
		if (!test_adapter) {
			hdd_comp_test_err("No suitable adapter found for comprehensive testing");
			return QDF_STATUS_E_FAILURE;
		}
	}

	hdd_comp_test_info("Using adapter %d (vdev_id=%d) for comprehensive testing",
			  test_adapter->device_mode, test_adapter->vdev_id);

	/* Ensure injection is initialized */
	if (!test_adapter->injection_ctx) {
		status = hdd_init_frame_injection(test_adapter);
		if (QDF_IS_STATUS_ERROR(status)) {
			hdd_comp_test_err("Failed to initialize injection for comprehensive tests: %d", status);
			return status;
		}
	}

	/* Enable injection for testing */
	status = hdd_frame_inject_enable(test_adapter);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_comp_test_warn("Failed to enable injection, continuing with limited testing: %d", status);
	}

	/* Major Test Suite 1: Unit Tests */
	hdd_comp_test_info("=== Major Test Suite 1: Unit Tests ===");
	major_test_suites_run++;
	status = hdd_execute_unit_test_suite(test_adapter);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		major_test_suites_passed++;
		hdd_comp_test_info("Unit test suite PASSED");
	} else {
		all_tests_passed = false;
		hdd_comp_test_err("Unit test suite FAILED: %d", status);
	}

	/* Major Test Suite 2: Integration Tests */
	hdd_comp_test_info("=== Major Test Suite 2: Integration Tests ===");
	major_test_suites_run++;
	status = hdd_execute_integration_test_suite(hdd_ctx);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		major_test_suites_passed++;
		hdd_comp_test_info("Integration test suite PASSED");
	} else {
		all_tests_passed = false;
		hdd_comp_test_err("Integration test suite FAILED: %d", status);
	}

	/* Major Test Suite 3: Security Validation */
	hdd_comp_test_info("=== Major Test Suite 3: Security Validation ===");
	major_test_suites_run++;
	status = hdd_security_validation_test_suite(test_adapter);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		major_test_suites_passed++;
		hdd_comp_test_info("Security validation test suite PASSED");
	} else {
		all_tests_passed = false;
		hdd_comp_test_err("Security validation test suite FAILED: %d", status);
	}

	/* Major Test Suite 4: Performance Tests */
	hdd_comp_test_info("=== Major Test Suite 4: Performance Tests ===");
	major_test_suites_run++;
	bool perf_tests_passed = true;

	/* Test different load conditions */
	for (uint32_t load_type = 1; load_type <= 4; load_type++) {
		hdd_comp_test_info("--- Performance Test %u: Load Type %u ---", load_type, load_type);
		
		status = hdd_test_performance_under_load(test_adapter, load_type, &perf_stats);
		if (QDF_IS_STATUS_ERROR(status)) {
			perf_tests_passed = false;
			hdd_comp_test_err("Performance test %u FAILED: %d", load_type, status);
		} else {
			hdd_comp_test_info("Performance test %u PASSED", load_type);
		}

		/* Brief pause between performance tests */
		qdf_sleep(1000);
	}

	if (perf_tests_passed) {
		major_test_suites_passed++;
		hdd_comp_test_info("Performance test suite PASSED");
	} else {
		all_tests_passed = false;
		hdd_comp_test_err("Performance test suite FAILED");
	}

	/* Major Test Suite 5: Extended Stability Test */
	hdd_comp_test_info("=== Major Test Suite 5: Extended Stability Test ===");
	major_test_suites_run++;
	
	/* Run stability test for 2 minutes (reduced for testing) */
	status = hdd_test_stability_extended_operation(test_adapter, 2);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		major_test_suites_passed++;
		hdd_comp_test_info("Extended stability test suite PASSED");
	} else {
		all_tests_passed = false;
		hdd_comp_test_err("Extended stability test suite FAILED: %d", status);
	}

	/* Cleanup */
	hdd_frame_inject_disable(test_adapter);

	/* Comprehensive test suite summary */
	hdd_comp_test_info("=== Comprehensive Test Suite Summary ===");
	hdd_comp_test_info("Major test suites run: %d", major_test_suites_run);
	hdd_comp_test_info("Major test suites passed: %d", major_test_suites_passed);
	hdd_comp_test_info("Major test suites failed: %d", major_test_suites_run - major_test_suites_passed);
	hdd_comp_test_info("Overall comprehensive test result: %s", 
			  all_tests_passed ? "PASSED" : "FAILED");

	if (all_tests_passed) {
		hdd_comp_test_info("🎉 All comprehensive tests PASSED! Frame injection system is ready for production.");
	} else {
		hdd_comp_test_err("❌ Some comprehensive tests FAILED. Review failures before production deployment.");
	}

	return all_tests_passed ? QDF_STATUS_SUCCESS : QDF_STATUS_E_FAILURE;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */