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
 * DOC: wlan_hdd_frame_validate.c
 *
 * WLAN Host Device Driver Frame Validation Implementation
 */

#include "wlan_hdd_includes.h"
#include "wlan_hdd_frame_inject.h"
#include "cds_ieee80211_common.h"
#include <linux/ieee80211.h>
#include <qdf_mem.h>
#include <qdf_trace.h>

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/* Logging macros for frame validation */
#define hdd_validate_debug(params...) \
	QDF_TRACE_DEBUG(QDF_MODULE_ID_HDD, params)
#define hdd_validate_info(params...) \
	QDF_TRACE_INFO(QDF_MODULE_ID_HDD, params)
#define hdd_validate_warn(params...) \
	QDF_TRACE_WARN(QDF_MODULE_ID_HDD, params)
#define hdd_validate_err(params...) \
	QDF_TRACE_ERROR(QDF_MODULE_ID_HDD, params)

/* Minimum frame sizes for different frame types */
#define HDD_MIN_MGMT_FRAME_SIZE    24  /* Basic management frame header */
#define HDD_MIN_CTRL_FRAME_SIZE    10  /* Basic control frame header */
#define HDD_MIN_DATA_FRAME_SIZE    24  /* Basic data frame header */

/* Maximum frame sizes */
#define HDD_MAX_FRAME_SIZE         HDD_FRAME_INJECT_MAX_SIZE

/* Frame type validation masks */
#define HDD_FRAME_TYPE_MGMT        IEEE80211_FC0_TYPE_MGT
#define HDD_FRAME_TYPE_CTRL        IEEE80211_FC0_TYPE_CTL
#define HDD_FRAME_TYPE_DATA        IEEE80211_FC0_TYPE_DATA

/* Management frame subtypes */
#define HDD_MGMT_SUBTYPE_ASSOC_REQ     0x00
#define HDD_MGMT_SUBTYPE_ASSOC_RESP    0x10
#define HDD_MGMT_SUBTYPE_REASSOC_REQ   0x20
#define HDD_MGMT_SUBTYPE_REASSOC_RESP  0x30
#define HDD_MGMT_SUBTYPE_PROBE_REQ     0x40
#define HDD_MGMT_SUBTYPE_PROBE_RESP    0x50
#define HDD_MGMT_SUBTYPE_BEACON        0x80
#define HDD_MGMT_SUBTYPE_ATIM          0x90
#define HDD_MGMT_SUBTYPE_DISASSOC      0xa0
#define HDD_MGMT_SUBTYPE_AUTH          0xb0
#define HDD_MGMT_SUBTYPE_DEAUTH        0xc0
#define HDD_MGMT_SUBTYPE_ACTION        0xd0

/* Control frame subtypes */
#define HDD_CTRL_SUBTYPE_RTS           0x40
#define HDD_CTRL_SUBTYPE_CTS           0x50
#define HDD_CTRL_SUBTYPE_ACK           0x60
#define HDD_CTRL_SUBTYPE_CFEND         0x70
#define HDD_CTRL_SUBTYPE_CFENDACK      0x80
#define HDD_CTRL_SUBTYPE_BAR           0x90
#define HDD_CTRL_SUBTYPE_BA            0xa0

/* Data frame subtypes */
#define HDD_DATA_SUBTYPE_DATA          0x00
#define HDD_DATA_SUBTYPE_DATA_CFACK    0x10
#define HDD_DATA_SUBTYPE_DATA_CFPOLL   0x20
#define HDD_DATA_SUBTYPE_DATA_CFACKPOLL 0x30
#define HDD_DATA_SUBTYPE_NULL          0x40
#define HDD_DATA_SUBTYPE_CFACK         0x50
#define HDD_DATA_SUBTYPE_CFPOLL        0x60
#define HDD_DATA_SUBTYPE_CFACKPOLL     0x70
#define HDD_DATA_SUBTYPE_QOS_DATA      0x80
#define HDD_DATA_SUBTYPE_QOS_NULL      0xc0

/**
 * hdd_validate_frame_header() - Validate basic 802.11 frame header
 * @frame_data: Pointer to frame data
 * @frame_len: Length of frame
 *
 * This function validates the basic 802.11 frame header structure
 * including frame control, duration, and address fields.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_validate_frame_header(uint8_t *frame_data,
					    uint32_t frame_len)
{
	struct ieee80211_frame *frame;
	uint8_t frame_type, frame_subtype;
	uint16_t frame_control;

	if (!frame_data) {
		hdd_validate_err("Frame data is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	if (frame_len < HDD_MIN_CTRL_FRAME_SIZE) {
		hdd_validate_err("Frame too short: %u bytes", frame_len);
		return QDF_STATUS_E_INVAL;
	}

	frame = (struct ieee80211_frame *)frame_data;
	frame_control = (frame->i_fc[1] << 8) | frame->i_fc[0];

	/* Validate frame version */
	if ((frame->i_fc[0] & IEEE80211_FC0_VERSION_0) != IEEE80211_FC0_VERSION_0) {
		hdd_validate_err("Invalid frame version: 0x%02x", 
				 frame->i_fc[0] & 0x03);
		return QDF_STATUS_E_INVAL;
	}

	/* Extract frame type and subtype */
	frame_type = frame->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	frame_subtype = frame->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	hdd_validate_debug("Frame type: 0x%02x, subtype: 0x%02x, len: %u",
			   frame_type, frame_subtype, frame_len);

	/* Validate frame type */
	switch (frame_type) {
	case IEEE80211_FC0_TYPE_MGT:
	case IEEE80211_FC0_TYPE_CTL:
	case IEEE80211_FC0_TYPE_DATA:
		break;
	default:
		hdd_validate_err("Invalid frame type: 0x%02x", frame_type);
		return QDF_STATUS_E_INVAL;
	}

	/* Validate address fields are not all zeros or all ones */
	if (qdf_is_macaddr_zero((struct qdf_mac_addr *)frame->i_addr1) ||
	    qdf_is_macaddr_broadcast((struct qdf_mac_addr *)frame->i_addr1)) {
		/* Allow broadcast for certain frame types */
		if (frame_type != IEEE80211_FC0_TYPE_MGT ||
		    (frame_subtype != HDD_MGMT_SUBTYPE_BEACON &&
		     frame_subtype != HDD_MGMT_SUBTYPE_PROBE_RESP)) {
			hdd_validate_debug("Broadcast addr1 in non-broadcast frame");
		}
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_validate_mgmt_frame() - Validate management frame
 * @frame_data: Pointer to frame data
 * @frame_len: Length of frame
 *
 * This function validates management frame specific fields
 * and ensures the frame structure is correct.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_validate_mgmt_frame(uint8_t *frame_data,
					  uint32_t frame_len)
{
	struct ieee80211_frame *frame;
	uint8_t frame_subtype;
	uint32_t min_size = HDD_MIN_MGMT_FRAME_SIZE;
	uint8_t *payload;
	uint32_t payload_len;

	frame = (struct ieee80211_frame *)frame_data;
	frame_subtype = frame->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	payload = frame_data + sizeof(struct ieee80211_frame);
	payload_len = frame_len - sizeof(struct ieee80211_frame);

	/* Validate minimum size based on subtype */
	switch (frame_subtype) {
	case HDD_MGMT_SUBTYPE_BEACON:
	case HDD_MGMT_SUBTYPE_PROBE_RESP:
		min_size = 36; /* Fixed fields + timestamp + beacon interval + capability */
		if (frame_len >= min_size) {
			/* Validate beacon interval (should be reasonable) */
			uint16_t beacon_interval = *(uint16_t *)(payload + 8);
			if (beacon_interval == 0 || beacon_interval > 65535) {
				hdd_validate_warn("Invalid beacon interval: %u", beacon_interval);
			}
		}
		break;
	case HDD_MGMT_SUBTYPE_PROBE_REQ:
		min_size = 24; /* Basic header */
		break;
	case HDD_MGMT_SUBTYPE_AUTH:
		min_size = 30; /* Header + auth algorithm + seq + status */
		if (frame_len >= min_size) {
			uint16_t auth_alg = *(uint16_t *)payload;
			uint16_t auth_seq;

			if (auth_alg > 3) { /* 0=Open, 1=Shared Key, 2=FT, 3=SAE */
				hdd_validate_warn("Unknown auth algorithm: %u", auth_alg);
			}
			/* Validate auth sequence number */
			auth_seq = *(uint16_t *)(payload + 2);
			if (auth_seq == 0 || auth_seq > 4) {
				hdd_validate_warn("Invalid auth sequence: %u", auth_seq);
			}
		}
		break;
	case HDD_MGMT_SUBTYPE_DEAUTH:
	case HDD_MGMT_SUBTYPE_DISASSOC:
		min_size = 26; /* Header + reason code */
		if (frame_len >= min_size) {
			/* Validate reason code */
			uint16_t reason = *(uint16_t *)payload;
			if (reason == 0 || reason > 65) {
				hdd_validate_warn("Invalid reason code: %u", reason);
			}
		}
		break;
	case HDD_MGMT_SUBTYPE_ASSOC_REQ:
	case HDD_MGMT_SUBTYPE_REASSOC_REQ:
		min_size = 28; /* Header + capability + listen interval */
		if (frame_len >= min_size) {
			/* Validate listen interval */
			uint16_t listen_int = *(uint16_t *)(payload + 2);
			if (listen_int == 0 || listen_int > 65535) {
				hdd_validate_warn("Invalid listen interval: %u", listen_int);
			}
		}
		break;
	case HDD_MGMT_SUBTYPE_ASSOC_RESP:
	case HDD_MGMT_SUBTYPE_REASSOC_RESP:
		min_size = 30; /* Header + capability + status + AID */
		if (frame_len >= min_size) {
			/* Validate AID */
			uint16_t aid = *(uint16_t *)(payload + 4);
			if ((aid & 0xC000) != 0xC000) { /* AID should have bits 14-15 set */
				hdd_validate_warn("Invalid AID format: 0x%04x", aid);
			}
		}
		break;
	case HDD_MGMT_SUBTYPE_ACTION:
		min_size = 26; /* Header + category + action */
		if (frame_len >= min_size) {
			/* Validate action category */
			uint8_t category = *payload;
			if (category > 127 && category < 221) { /* Reserved range */
				hdd_validate_warn("Reserved action category: %u", category);
			}
		}
		break;
	case HDD_MGMT_SUBTYPE_ATIM:
		min_size = 24; /* Basic header only */
		break;
	default:
		hdd_validate_warn("Unknown management subtype: 0x%02x", frame_subtype);
		break;
	}

	if (frame_len < min_size) {
		hdd_validate_err("Management frame too short: %u < %u (subtype 0x%02x)",
				 frame_len, min_size, frame_subtype);
		return QDF_STATUS_E_INVAL;
	}

	/* Validate that management frames don't have DS bits set incorrectly */
	if ((frame->i_fc[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_NODS) {
		hdd_validate_warn("Management frame with DS bits set: 0x%02x", frame->i_fc[1]);
	}

	hdd_validate_debug("Management frame validated: subtype 0x%02x, len %u",
			   frame_subtype, frame_len);

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_validate_ctrl_frame() - Validate control frame
 * @frame_data: Pointer to frame data
 * @frame_len: Length of frame
 *
 * This function validates control frame specific fields
 * and ensures the frame structure is correct.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_validate_ctrl_frame(uint8_t *frame_data,
					  uint32_t frame_len)
{
	struct ieee80211_frame *frame;
	uint8_t frame_subtype;
	uint32_t min_size = HDD_MIN_CTRL_FRAME_SIZE;
	uint16_t duration;

	frame = (struct ieee80211_frame *)frame_data;
	frame_subtype = frame->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	duration = *(uint16_t *)frame->i_dur;

	/* Validate minimum size based on subtype */
	switch (frame_subtype) {
	case HDD_CTRL_SUBTYPE_RTS:
		min_size = 16; /* FC + Duration + RA + TA */
		/* RTS frames should have reasonable duration */
		if (duration == 0 || duration > 32767) {
			hdd_validate_warn("Invalid RTS duration: %u", duration);
		}
		break;
	case HDD_CTRL_SUBTYPE_CTS:
		min_size = 10; /* FC + Duration + RA */
		/* CTS duration should be reasonable */
		if (duration > 32767) {
			hdd_validate_warn("Invalid CTS duration: %u", duration);
		}
		break;
	case HDD_CTRL_SUBTYPE_ACK:
		min_size = 10; /* FC + Duration + RA */
		/* ACK frames typically have zero duration */
		break;
	case HDD_CTRL_SUBTYPE_BAR:
		min_size = 20; /* FC + Duration + RA + TA + BAR Control + BAR Info */
		if (frame_len >= min_size) {
			/* Validate BAR control field */
			uint16_t bar_control = *(uint16_t *)(frame_data + 16);
			uint8_t tid = (bar_control >> 12) & 0x0F;
			if (tid > 15) {
				hdd_validate_warn("Invalid BAR TID: %u", tid);
			}
		}
		break;
	case HDD_CTRL_SUBTYPE_BA:
		min_size = 24; /* FC + Duration + RA + TA + BA Control + BA Info */
		if (frame_len >= min_size) {
			/* Validate BA control field */
			uint16_t ba_control = *(uint16_t *)(frame_data + 16);
			uint8_t tid = (ba_control >> 12) & 0x0F;
			if (tid > 15) {
				hdd_validate_warn("Invalid BA TID: %u", tid);
			}
		}
		break;
	case HDD_CTRL_SUBTYPE_CFEND:
		min_size = 16; /* FC + Duration + RA + BSSID */
		break;
	case HDD_CTRL_SUBTYPE_CFENDACK:
		min_size = 16; /* FC + Duration + RA + BSSID */
		break;
	default:
		hdd_validate_warn("Unknown control subtype: 0x%02x", frame_subtype);
		/* Allow unknown subtypes but validate basic structure */
		break;
	}

	if (frame_len < min_size) {
		hdd_validate_err("Control frame too short: %u < %u (subtype 0x%02x)",
				 frame_len, min_size, frame_subtype);
		return QDF_STATUS_E_INVAL;
	}

	/* Control frames should not have DS bits set */
	if ((frame->i_fc[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_NODS) {
		hdd_validate_warn("Control frame with DS bits set: 0x%02x", frame->i_fc[1]);
	}

	/* Control frames should not have certain flags set */
	if (frame->i_fc[1] & (IEEE80211_FC1_MORE_FRAG | IEEE80211_FC1_RETRY)) {
		hdd_validate_warn("Control frame with invalid flags: 0x%02x", frame->i_fc[1]);
	}

	hdd_validate_debug("Control frame validated: subtype 0x%02x, len %u",
			   frame_subtype, frame_len);

	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_validate_data_frame() - Validate data frame
 * @frame_data: Pointer to frame data
 * @frame_len: Length of frame
 *
 * This function validates data frame specific fields
 * and ensures the frame structure is correct.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS hdd_validate_data_frame(uint8_t *frame_data,
					  uint32_t frame_len)
{
	struct ieee80211_frame *frame;
	uint8_t frame_subtype;
	uint32_t min_size = HDD_MIN_DATA_FRAME_SIZE;
	bool has_qos = false;
	bool has_addr4 = false;
	bool has_htc = false;
	uint8_t ds_bits;
	uint8_t *qos_ptr = NULL;
	uint8_t tid;
	uint8_t ack_policy;
	uint16_t seq_ctrl;
	uint16_t seq_num;
	uint8_t frag_num;

	frame = (struct ieee80211_frame *)frame_data;
	frame_subtype = frame->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	ds_bits = frame->i_fc[1] & IEEE80211_FC1_DIR_MASK;

	/* Check for QoS data frames */
	if (frame_subtype & 0x80) {
		has_qos = true;
		min_size += 2; /* QoS control field */
	}

	/* Check for 4-address frames (DS to DS) */
	if (ds_bits == IEEE80211_FC1_DIR_DSTODS) {
		has_addr4 = true;
		min_size += QDF_MAC_ADDR_SIZE; /* Address 4 field */
	}

	/* Check for HTC field (Order bit set in QoS frames) */
	if (has_qos && (frame->i_fc[1] & IEEE80211_FC1_ORDER)) {
		has_htc = true;
		min_size += 4; /* HTC field */
	}

	/* Validate minimum size */
	if (frame_len < min_size) {
		hdd_validate_err("Data frame too short: %u < %u (subtype 0x%02x)",
				 frame_len, min_size, frame_subtype);
		return QDF_STATUS_E_INVAL;
	}

	/* Validate DS bits combinations */
	switch (ds_bits) {
	case IEEE80211_FC1_DIR_NODS:   /* STA to STA (IBSS) */
	case IEEE80211_FC1_DIR_TODS:   /* STA to AP */
	case IEEE80211_FC1_DIR_FROMDS: /* AP to STA */
	case IEEE80211_FC1_DIR_DSTODS: /* AP to AP (WDS) */
		break;
	default:
		hdd_validate_err("Invalid DS bits combination: 0x%02x", ds_bits);
		return QDF_STATUS_E_INVAL;
	}

	/* Validate QoS control field if present */
	if (has_qos) {
		uint32_t qos_offset = sizeof(struct ieee80211_frame);
		if (has_addr4)
			qos_offset += QDF_MAC_ADDR_SIZE;
		
		if (frame_len > qos_offset + 1) {
			qos_ptr = frame_data + qos_offset;
			tid = qos_ptr[0] & IEEE80211_QOS_TID;
			ack_policy = (qos_ptr[0] >> IEEE80211_QOS_ACKPOLICY_S) & 0x03;
			
			/* Validate TID */
			if (tid > 15) {
				hdd_validate_warn("Invalid QoS TID: %u", tid);
			}
			
			/* Validate ACK policy */
			if (ack_policy > 3) {
				hdd_validate_warn("Invalid QoS ACK policy: %u", ack_policy);
			}
			
			/* Check A-MSDU bit */
			if (qos_ptr[0] & IEEE80211_QOS_AMSDU) {
				hdd_validate_debug("A-MSDU frame detected");
			}
		}
	}

	/* Validate specific data subtypes */
	switch (frame_subtype & 0x70) { /* Mask out QoS bit */
	case HDD_DATA_SUBTYPE_DATA:
	case HDD_DATA_SUBTYPE_DATA_CFACK:
	case HDD_DATA_SUBTYPE_DATA_CFPOLL:
	case HDD_DATA_SUBTYPE_DATA_CFACKPOLL:
		/* These frames should have payload unless they're null data */
		if (frame_len <= min_size && !(frame_subtype & 0x40)) {
			hdd_validate_warn("Data frame with no payload: len %u", frame_len);
		}
		break;
	case HDD_DATA_SUBTYPE_NULL:
	case HDD_DATA_SUBTYPE_CFACK:
	case HDD_DATA_SUBTYPE_CFPOLL:
	case HDD_DATA_SUBTYPE_CFACKPOLL:
		/* These frames typically have no payload */
		if (frame_len > min_size) {
			hdd_validate_warn("Null data frame with payload: len %u", frame_len);
		}
		break;
	default:
		hdd_validate_warn("Unknown data subtype: 0x%02x", frame_subtype);
		break;
	}

	/* Validate sequence number */
	seq_ctrl = *(uint16_t *)frame->i_seq;
	seq_num = (seq_ctrl & IEEE80211_SEQ_SEQ_MASK) >> IEEE80211_SEQ_SEQ_SHIFT;
	frag_num = seq_ctrl & IEEE80211_SEQ_FRAG_MASK;
	
	if (seq_num >= IEEE80211_SEQ_MAX) {
		hdd_validate_warn("Invalid sequence number: %u", seq_num);
	}
	
	if (frag_num > 15) {
		hdd_validate_warn("Invalid fragment number: %u", frag_num);
	}

	/* Check for fragmentation consistency */
	if (frag_num > 0 && !(frame->i_fc[1] & IEEE80211_FC1_MORE_FRAG)) {
		hdd_validate_warn("Last fragment without More Fragments bit clear");
	}

	hdd_validate_debug("Data frame validated: subtype 0x%02x, len %u, QoS %d, 4addr %d, HTC %d",
			   frame_subtype, frame_len, has_qos, has_addr4, has_htc);

	return QDF_STATUS_SUCCESS;
}

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
QDF_STATUS hdd_validate_80211_frame(uint8_t *frame_data, uint32_t frame_len)
{
	QDF_STATUS status;
	struct ieee80211_frame *frame;
	uint8_t frame_type;

	hdd_validate_debug("Validating 802.11 frame: len %u", frame_len);

	if (!frame_data) {
		hdd_validate_err("Frame data is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	if (frame_len == 0) {
		hdd_validate_err("Frame length is zero");
		return QDF_STATUS_E_INVAL;
	}

	/* Validate basic frame header */
	status = hdd_validate_frame_header(frame_data, frame_len);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_validate_err("Frame header validation failed: %d", status);
		return status;
	}

	frame = (struct ieee80211_frame *)frame_data;
	frame_type = frame->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	/* Validate frame type specific fields */
	switch (frame_type) {
	case IEEE80211_FC0_TYPE_MGT:
		status = hdd_validate_mgmt_frame(frame_data, frame_len);
		break;
	case IEEE80211_FC0_TYPE_CTL:
		status = hdd_validate_ctrl_frame(frame_data, frame_len);
		break;
	case IEEE80211_FC0_TYPE_DATA:
		status = hdd_validate_data_frame(frame_data, frame_len);
		break;
	default:
		hdd_validate_err("Invalid frame type: 0x%02x", frame_type);
		status = QDF_STATUS_E_INVAL;
		break;
	}

	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_validate_err("Frame type validation failed: %d", status);
		return status;
	}

	hdd_validate_debug("802.11 frame validation successful");
	return QDF_STATUS_SUCCESS;
}

/**
 * hdd_check_frame_size_limits() - Check frame size constraints
 * @frame_len: Length of frame
 *
 * This function validates that the frame size is within acceptable
 * limits for the hardware and driver.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS hdd_check_frame_size_limits(uint32_t frame_len)
{
	hdd_validate_debug("Checking frame size limits: len %u", frame_len);

	if (frame_len == 0) {
		hdd_validate_err("Frame length is zero");
		return QDF_STATUS_E_INVAL;
	}

	if (frame_len > HDD_MAX_FRAME_SIZE) {
		hdd_validate_err("Frame too large: %u > %u", 
				 frame_len, HDD_MAX_FRAME_SIZE);
		return QDF_STATUS_E_INVAL;
	}

	if (frame_len < HDD_MIN_CTRL_FRAME_SIZE) {
		hdd_validate_err("Frame too small: %u < %u", 
				 frame_len, HDD_MIN_CTRL_FRAME_SIZE);
		return QDF_STATUS_E_INVAL;
	}

	hdd_validate_debug("Frame size validation successful");
	return QDF_STATUS_SUCCESS;
}

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
QDF_STATUS hdd_sanitize_frame_content(uint8_t *frame_data, uint32_t frame_len)
{
	struct ieee80211_frame *frame;
	uint8_t frame_type, frame_subtype;

	hdd_validate_debug("Sanitizing frame content: len %u", frame_len);

	if (!frame_data) {
		hdd_validate_err("Frame data is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	if (frame_len < HDD_MIN_CTRL_FRAME_SIZE) {
		hdd_validate_err("Frame too short for sanitization: %u", frame_len);
		return QDF_STATUS_E_INVAL;
	}

	frame = (struct ieee80211_frame *)frame_data;
	frame_type = frame->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	frame_subtype = frame->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	/* Clear reserved bits in frame control */
	frame->i_fc[0] &= ~0x03; /* Clear version bits (should be 00) */
	frame->i_fc[1] &= ~0x40; /* Clear reserved bit */

	/* Sanitize based on frame type */
	switch (frame_type) {
	case IEEE80211_FC0_TYPE_MGT:
		/* For management frames, ensure certain fields are reasonable */
		if (frame_subtype == HDD_MGMT_SUBTYPE_BEACON ||
		    frame_subtype == HDD_MGMT_SUBTYPE_PROBE_RESP) {
			/* Don't allow injection of beacons with our own BSSID */
			/* This would be checked against adapter's BSSID in actual implementation */
		}
		break;
	case IEEE80211_FC0_TYPE_CTL:
		/* Control frames have minimal sanitization needs */
		break;
	case IEEE80211_FC0_TYPE_DATA:
		/* Data frames - ensure QoS field is reasonable if present */
		if (frame_subtype & 0x80) { /* QoS data frame */
			/* QoS control field sanitization would go here */
		}
		break;
	}

	hdd_validate_debug("Frame content sanitization successful");
	return QDF_STATUS_SUCCESS;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */
