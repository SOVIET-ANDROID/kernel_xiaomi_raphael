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

#ifndef __WLAN_HDD_FRAME_INJECT_SECURITY_TEST_H
#define __WLAN_HDD_FRAME_INJECT_SECURITY_TEST_H

/**
 * DOC: wlan_hdd_frame_inject_security_test.h
 *
 * WLAN Host Device Driver Frame Injection Security Validation Test APIs
 */

#include <qdf_types.h>
#include <qdf_status.h>

/* Forward declarations */
struct hdd_adapter;

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/**
 * hdd_test_capability_checking() - Test capability checking with various process contexts
 * @adapter: HDD adapter
 *
 * This function tests the capability checking mechanism with different process
 * contexts to ensure proper access control.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_test_capability_checking(struct hdd_adapter *adapter);

/**
 * hdd_test_rate_limiting_attack_scenarios() - Test rate limiting under attack scenarios
 * @adapter: HDD adapter
 *
 * This function tests the rate limiting mechanism under various attack scenarios
 * to ensure it effectively prevents DoS attacks.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_test_rate_limiting_attack_scenarios(struct hdd_adapter *adapter);

/**
 * hdd_test_audit_logging_completeness() - Test audit logging completeness and accuracy
 * @adapter: HDD adapter
 *
 * This function tests the audit logging system to ensure all security events
 * are properly logged with accurate information.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_test_audit_logging_completeness(struct hdd_adapter *adapter);

/**
 * hdd_security_validation_test_suite() - Run complete security validation test suite
 * @adapter: HDD adapter
 *
 * This function runs the complete security validation test suite for frame injection,
 * covering capability checking, rate limiting, and audit logging.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code on failure
 */
QDF_STATUS hdd_security_validation_test_suite(struct hdd_adapter *adapter);

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline QDF_STATUS hdd_test_capability_checking(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_test_rate_limiting_attack_scenarios(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_test_audit_logging_completeness(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_security_validation_test_suite(struct hdd_adapter *adapter)
{
	return QDF_STATUS_E_NOSUPPORT;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WLAN_HDD_FRAME_INJECT_SECURITY_TEST_H */