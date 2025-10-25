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

#ifndef __WLAN_HDD_FRAME_VALIDATE_H
#define __WLAN_HDD_FRAME_VALIDATE_H

/**
 * DOC: wlan_hdd_frame_validate.h
 *
 * WLAN Host Device Driver Frame Validation APIs
 */

#include <qdf_types.h>
#include <qdf_status.h>

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/**
 * hdd_validate_80211_frame() - Validate 802.11 frame format
 * @frame_data: Pointer to frame data
 * @frame_len: Length of frame
 *
 * This function performs comprehensive validation of 802.11 frame
 * format including header structure and frame type specific checks.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_validate_80211_frame(uint8_t *frame_data, uint32_t frame_len);

/**
 * hdd_check_frame_size_limits() - Check frame size constraints
 * @frame_len: Length of frame
 *
 * This function validates that the frame size is within acceptable
 * limits for the hardware and driver.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_check_frame_size_limits(uint32_t frame_len);

/**
 * hdd_sanitize_frame_content() - Sanitize frame content for security
 * @frame_data: Pointer to frame data
 * @frame_len: Length of frame
 *
 * This function performs security sanitization of frame content
 * to prevent potential security issues.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_sanitize_frame_content(uint8_t *frame_data, uint32_t frame_len);

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

static inline QDF_STATUS hdd_validate_80211_frame(uint8_t *frame_data, 
						   uint32_t frame_len)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_check_frame_size_limits(uint32_t frame_len)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS hdd_sanitize_frame_content(uint8_t *frame_data, 
						    uint32_t frame_len)
{
	return QDF_STATUS_E_NOSUPPORT;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */

#endif /* __WLAN_HDD_FRAME_VALIDATE_H */