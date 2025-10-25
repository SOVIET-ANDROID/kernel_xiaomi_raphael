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

#ifndef __WLAN_HDD_FRAME_VALIDATE_TEST_H
#define __WLAN_HDD_FRAME_VALIDATE_TEST_H

/**
 * DOC: wlan_hdd_frame_validate_test.h
 *
 * WLAN Host Device Driver Frame Validation Unit Test APIs
 */

#include <qdf_types.h>
#include <qdf_status.h>

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/**
 * hdd_run_frame_validation_tests() - Run all frame validation tests
 *
 * This function runs the complete suite of frame validation tests.
 *
 * Return: QDF_STATUS_SUCCESS if all tests pass, error code otherwise
 */
QDF_STATUS hdd_run_frame_validation_tests(void);

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline QDF_STATUS hdd_run_frame_validation_tests(void)
{
	return QDF_STATUS_E_NOSUPPORT;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WLAN_HDD_FRAME_VALIDATE_TEST_H */