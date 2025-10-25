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
 * DOC: wlan_hdd_frame_validate_test.c
 *
 * WLAN Host Device Driver Frame Validation Unit Tests
 */

#include "wlan_hdd_includes.h"
#include "wlan_hdd_frame_validate.h"
#include "cds_ieee80211_common.h"
#include <qdf_mem.h>
#include <qdf_trace.h>

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/* Test logging macros */
#define hdd_test_debug(params...) \
	QDF_TRACE_DEBUG(QDF_MODULE_ID_HDD, "TEST: " params)
#define hdd_test_info(params...) \
	QDF_TRACE_INFO(QDF_MODULE_ID_HDD, "TEST: " params)
#define hdd_test_err(params...) \
	QDF_TRACE_ERROR(QDF_MODULE_ID_HDD, "TEST: " params)

/* Test result tracking */
static uint32_t g_tests_run = 0;
static uint32_t g_tests_passed = 0;
static uint32_t g_tests_failed = 0;

#define HDD_TEST_ASSERT(condition, msg) \
	do { \
		g_tests_run++; \
		if (condition) { \
			g_tests_passed++; \
			hdd_test_debug("PASS: %s", msg); \
		} else { \
			g_tests_failed++; \
			hdd_test_err("FAIL: %s", msg); \
		} \
	} while (0)

/**
 * hdd_test_create_mgmt_frame() - Create a test management frame
 * @subtype: Management frame subtype
 * @frame_len: Desired frame length
 * @frame_data: Output buffer for frame data
 *
 * This function creates a basic management frame for testing.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_test_create_mgmt_frame(uint8_t subtype, uint32_t frame_len,
					     uint8_t *frame_data)
{
	struct ieee80211_frame *frame;
	uint8_t test_addr[QDF_MAC_ADDR_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

	if (!frame_data || frame_len < sizeof(struct ieee80211_frame))
		return QDF_STATUS_E_INVAL;

	qdf_mem_zero(frame_data, frame_len);
	frame = (struct ieee80211_frame *)frame_data;

	/* Set frame control */
	frame->i_fc[0] = IEEE80211_FC0_TYPE_MGT | subtype;
	frame->i_fc[1] = 0;

	/* Set duration */
	*(uint16_t *)frame->i_dur = 0x0000;

	/* Set addresses */
	qdf_mem_copy(frame->i_addr1, test_addr, QDF_MAC_ADDR_SIZE);
	qdf_mem_copy(frame->i_addr2, test_addr, QDF_MAC_ADDR_SIZE);
	qdf_mem_copy(frame->i_addr3, test_addr, QDF_MAC_ADDR_SIZE);

	/* Set sequence control */
	*(uint16_t *)frame->i_seq = 0x1234;

	/* Add subtype-specific fields */
	if (frame_len > sizeof(struct ieee80211_frame)) {
		uint8_t *payload = frame_data + sizeof(struct ieee80211_frame);
		
		switch (subtype) {
		case 0x80: /* Beacon */
		case 0x50: /* Probe Response */
			if (frame_len >= 36) {
				/* Timestamp */
				*(uint64_t *)payload = 0x123456789ABCDEF0ULL;
				/* Beacon interval */
				*(uint16_t *)(payload + 8) = 100;
				/* Capability info */
				*(uint16_t *)(payload + 10) = 0x1234;
			}
			break;
		case 0xb0: /* Authentication */
			if (frame_len >= 30) {
				/* Auth algorithm */
				*(uint16_t *)payload = 0; /* Open system */
				/* Auth sequence */
				*(uint16_t *)(payload + 2) = 1;
				/* Status code */
				*(uint16_t *)(payload + 4) = 0;
			}
			break;
		case 0xc0: /* Deauthentication */
		case 0xa0: /* Disassociation */
			if (frame_len >= 26) {
				/* Reason code */
				*(uint16_t *)payload = 1;
			}
			break;
		}
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_test_create_ctrl_frame() - Create a test control frame
 * @subtype: Control frame subtype
 * @frame_len: Desired frame length
 * @frame_data: Output buffer for frame data
 *
 * This function creates a basic control frame for testing.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_test_create_ctrl_frame(uint8_t subtype, uint32_t frame_len,
					     uint8_t *frame_data)
{
	uint8_t test_addr[QDF_MAC_ADDR_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

	if (!frame_data || frame_len < 10)
		return QDF_STATUS_E_INVAL;

	qdf_mem_zero(frame_data, frame_len);

	/* Set frame control */
	frame_data[0] = IEEE80211_FC0_TYPE_CTL | subtype;
	frame_data[1] = 0;

	/* Set duration */
	*(uint16_t *)(frame_data + 2) = 0x1234;

	/* Set receiver address */
	qdf_mem_copy(frame_data + 4, test_addr, QDF_MAC_ADDR_SIZE);

	/* Add subtype-specific fields */
	switch (subtype) {
	case 0x40: /* RTS */
		if (frame_len >= 16) {
			/* Transmitter address */
			qdf_mem_copy(frame_data + 10, test_addr, QDF_MAC_ADDR_SIZE);
		}
		break;
	case 0x90: /* BAR */
		if (frame_len >= 20) {
			/* Transmitter address */
			qdf_mem_copy(frame_data + 10, test_addr, QDF_MAC_ADDR_SIZE);
			/* BAR control */
			*(uint16_t *)(frame_data + 16) = 0x1000; /* TID 1 */
			/* BAR information */
			*(uint16_t *)(frame_data + 18) = 0x1000;
		}
		break;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_test_create_data_frame() - Create a test data frame
 * @subtype: Data frame subtype
 * @frame_len: Desired frame length
 * @frame_data: Output buffer for frame data
 *
 * This function creates a basic data frame for testing.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_test_create_data_frame(uint8_t subtype, uint32_t frame_len,
					     uint8_t *frame_data)
{
	struct ieee80211_frame *frame;
	uint8_t test_addr[QDF_MAC_ADDR_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	bool has_qos = (subtype & 0x80) != 0;
	uint32_t header_len = sizeof(struct ieee80211_frame);

	if (!frame_data || frame_len < header_len)
		return QDF_STATUS_E_INVAL;

	qdf_mem_zero(frame_data, frame_len);
	frame = (struct ieee80211_frame *)frame_data;

	/* Set frame control */
	frame->i_fc[0] = IEEE80211_FC0_TYPE_DATA | subtype;
	frame->i_fc[1] = IEEE80211_FC1_DIR_TODS; /* STA to AP */

	/* Set duration */
	*(uint16_t *)frame->i_dur = 0x1234;

	/* Set addresses */
	qdf_mem_copy(frame->i_addr1, test_addr, QDF_MAC_ADDR_SIZE); /* BSSID */
	qdf_mem_copy(frame->i_addr2, test_addr, QDF_MAC_ADDR_SIZE); /* SA */
	qdf_mem_copy(frame->i_addr3, test_addr, QDF_MAC_ADDR_SIZE); /* DA */

	/* Set sequence control */
	*(uint16_t *)frame->i_seq = 0x1234;

	/* Add QoS control if needed */
	if (has_qos && frame_len > header_len + 1) {
		uint8_t *qos_ptr = frame_data + header_len;
		qos_ptr[0] = 0x01; /* TID 1 */
		qos_ptr[1] = 0x00;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_test_frame_size_limits() - Test frame size limit validation
 *
 * This function tests the frame size limit validation function.
 */
static void hdd_test_frame_size_limits(void)
{
	QDF_STATUS status;

	hdd_test_info("Testing frame size limits");

	/* Test zero length */
	status = hdd_check_frame_size_limits(0);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Zero length frame rejected");

	/* Test minimum valid size */
	status = hdd_check_frame_size_limits(10);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Minimum valid size accepted");

	/* Test normal size */
	status = hdd_check_frame_size_limits(1500);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Normal size accepted");

	/* Test maximum size */
	status = hdd_check_frame_size_limits(HDD_FRAME_INJECT_MAX_SIZE);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Maximum size accepted");

	/* Test oversized frame */
	status = hdd_check_frame_size_limits(HDD_FRAME_INJECT_MAX_SIZE + 1);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Oversized frame rejected");

	/* Test very large frame */
	status = hdd_check_frame_size_limits(65536);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Very large frame rejected");
}

/**
 * hdd_test_mgmt_frame_validation() - Test management frame validation
 *
 * This function tests management frame validation.
 */
static void hdd_test_mgmt_frame_validation(void)
{
	uint8_t frame_data[512];
	QDF_STATUS status;

	hdd_test_info("Testing management frame validation");

	/* Test valid beacon frame */
	status = hdd_test_create_mgmt_frame(0x80, 100, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Beacon frame created");
	
	status = hdd_validate_80211_frame(frame_data, 100);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Valid beacon frame accepted");

	/* Test valid authentication frame */
	status = hdd_test_create_mgmt_frame(0xb0, 30, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Auth frame created");
	
	status = hdd_validate_80211_frame(frame_data, 30);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Valid auth frame accepted");

	/* Test undersized management frame */
	status = hdd_validate_80211_frame(frame_data, 20);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Undersized mgmt frame rejected");

	/* Test NULL pointer */
	status = hdd_validate_80211_frame(NULL, 100);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "NULL frame data rejected");

	/* Test zero length */
	status = hdd_validate_80211_frame(frame_data, 0);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Zero length frame rejected");
}

/**
 * hdd_test_ctrl_frame_validation() - Test control frame validation
 *
 * This function tests control frame validation.
 */
static void hdd_test_ctrl_frame_validation(void)
{
	uint8_t frame_data[64];
	QDF_STATUS status;

	hdd_test_info("Testing control frame validation");

	/* Test valid RTS frame */
	status = hdd_test_create_ctrl_frame(0x40, 16, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "RTS frame created");
	
	status = hdd_validate_80211_frame(frame_data, 16);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Valid RTS frame accepted");

	/* Test valid CTS frame */
	status = hdd_test_create_ctrl_frame(0x50, 10, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "CTS frame created");
	
	status = hdd_validate_80211_frame(frame_data, 10);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Valid CTS frame accepted");

	/* Test valid ACK frame */
	status = hdd_test_create_ctrl_frame(0x60, 10, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "ACK frame created");
	
	status = hdd_validate_80211_frame(frame_data, 10);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Valid ACK frame accepted");

	/* Test undersized control frame */
	status = hdd_validate_80211_frame(frame_data, 8);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Undersized ctrl frame rejected");
}

/**
 * hdd_test_data_frame_validation() - Test data frame validation
 *
 * This function tests data frame validation.
 */
static void hdd_test_data_frame_validation(void)
{
	uint8_t frame_data[512];
	QDF_STATUS status;

	hdd_test_info("Testing data frame validation");

	/* Test valid data frame */
	status = hdd_test_create_data_frame(0x00, 100, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Data frame created");
	
	status = hdd_validate_80211_frame(frame_data, 100);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Valid data frame accepted");

	/* Test valid QoS data frame */
	status = hdd_test_create_data_frame(0x80, 100, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "QoS data frame created");
	
	status = hdd_validate_80211_frame(frame_data, 100);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Valid QoS data frame accepted");

	/* Test valid null data frame */
	status = hdd_test_create_data_frame(0x40, 24, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Null data frame created");
	
	status = hdd_validate_80211_frame(frame_data, 24);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Valid null data frame accepted");

	/* Test undersized data frame */
	status = hdd_validate_80211_frame(frame_data, 20);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Undersized data frame rejected");
}

/**
 * hdd_test_frame_sanitization() - Test frame content sanitization
 *
 * This function tests frame content sanitization.
 */
static void hdd_test_frame_sanitization(void)
{
	uint8_t frame_data[128];
	QDF_STATUS status;
	struct ieee80211_frame *frame;

	hdd_test_info("Testing frame sanitization");

	/* Create a frame with invalid version bits */
	status = hdd_test_create_mgmt_frame(0x80, 100, frame_data);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Test frame created");

	frame = (struct ieee80211_frame *)frame_data;
	frame->i_fc[0] |= 0x03; /* Set invalid version bits */

	/* Sanitize the frame */
	status = hdd_sanitize_frame_content(frame_data, 100);
	HDD_TEST_ASSERT(QDF_IS_STATUS_SUCCESS(status), "Frame sanitization successful");

	/* Check that version bits were cleared */
	HDD_TEST_ASSERT((frame->i_fc[0] & 0x03) == 0, "Version bits cleared");

	/* Test NULL pointer */
	status = hdd_sanitize_frame_content(NULL, 100);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "NULL frame data rejected");

	/* Test undersized frame */
	status = hdd_sanitize_frame_content(frame_data, 10);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Undersized frame rejected");
}

/**
 * hdd_test_invalid_frame_types() - Test invalid frame type handling
 *
 * This function tests handling of invalid frame types.
 */
static void hdd_test_invalid_frame_types(void)
{
	uint8_t frame_data[64];
	QDF_STATUS status;
	struct ieee80211_frame *frame;

	hdd_test_info("Testing invalid frame types");

	/* Create a basic frame */
	qdf_mem_zero(frame_data, sizeof(frame_data));
	frame = (struct ieee80211_frame *)frame_data;

	/* Set invalid frame type */
	frame->i_fc[0] = 0x0C; /* Reserved frame type */
	frame->i_fc[1] = 0x00;

	status = hdd_validate_80211_frame(frame_data, 24);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Invalid frame type rejected");

	/* Test invalid version */
	frame->i_fc[0] = IEEE80211_FC0_TYPE_MGT | 0x01; /* Invalid version */
	status = hdd_validate_80211_frame(frame_data, 24);
	HDD_TEST_ASSERT(QDF_IS_STATUS_ERROR(status), "Invalid version rejected");
}

/**
 * hdd_run_frame_validation_tests() - Run all frame validation tests
 *
 * This function runs the complete suite of frame validation tests.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code otherwise
 */
QDF_STATUS hdd_run_frame_validation_tests(void)
{
	hdd_test_info("Starting frame validation unit tests");

	/* Reset test counters */
	g_tests_run = 0;
	g_tests_passed = 0;
	g_tests_failed = 0;

	/* Run test suites */
	hdd_test_frame_size_limits();
	hdd_test_mgmt_frame_validation();
	hdd_test_ctrl_frame_validation();
	hdd_test_data_frame_validation();
	hdd_test_frame_sanitization();
	hdd_test_invalid_frame_types();

	/* Print test results */
	hdd_test_info("Frame validation tests completed:");
	hdd_test_info("  Total tests: %u", g_tests_run);
	hdd_test_info("  Passed: %u", g_tests_passed);
	hdd_test_info("  Failed: %u", g_tests_failed);

	if (g_tests_failed == 0) {
		hdd_test_info("All frame validation tests PASSED");
		return QDF_STATUS_SUCCESS;
	} else {
		hdd_test_err("%u frame validation tests FAILED", g_tests_failed);
		return QDF_STATUS_E_FAILURE;
	}
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */