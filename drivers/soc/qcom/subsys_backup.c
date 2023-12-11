/* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/delay.h>

#define SUBSYS_BACKUP_SVC_ID 0x54
#define SUBSYS_BACKUP_SVC_VERS 1
#define SUBSYS_BACKUP_SVC_INS_ID 0

#define QMI_BACKUP_IND_REG_REQ_MSG_ID 0x0001
#define QMI_BACKUP_IND_REG_RESP_MSG_ID 0x0001
#define QMI_RESTORE_IND_REG_REQ_MSG_ID 0x0002
#define QMI_RESTORE_IND_REG_RESP_MSG_ID 0x0002
#define QMI_BACKUP_IND_MSG_ID 0x0003
#define QMI_RESTORE_IND_MSG_ID 0x0004
#define QMI_BACKUP_MEM_READY_REQ_MSG_ID 0x0005
#define QMI_BACKUP_MEM_READY_RESP_MSG_ID 0x0005
#define QMI_RESTORE_MEM_READY_REQ_MSG_ID 0x0006
#define QMI_RESTORE_MEM_READY_RESP_MSG_ID 0x0006

#define UEVENT_STRING_MAX_SIZE 22

enum qmi_backup_restore_state {
	START,
	END,
};

/**
 * State of the driver.
 */
enum process_state {
	IDLE,
	BACKUP_START,
	BACKUP_END,
	RESTORE_START,
	RESTORE_END,
};

/**
 * Notifications that are sent by the kernel to the remote
 * subsystem and the userspace.
 */
enum notif_type {
	BACKUP_NOTIF_START,
	BACKUP_NOTIF_END,
	RESTORE_NOTIF_START,
	RESTORE_NOTIF_END,
	BACKUP_ALLOC_SUCCESS,
	BACKUP_ALLOC_FAIL,
	RESTORE_ALLOC_SUCCESS,
	RESTORE_ALLOC_FAIL,
};

enum qmi_backup_type {
	BACKUP_TYPE_FACTORY,
	BACKUP_TYPE_RUNTIME,
	BACKUP_SOFTWARE_ROLL_BACK
};

enum qmi_remote_status {
	SUCCESS,
	FAILED,
	ADDR_VALID,
	ADDR_INVALID
};

struct qmi_backup_ind_type {
	u8 backup_state;
	u32 backup_image_size;
	u8 qmi_backup_type;
	u8 backup_status;
};

struct qmi_restore_ind_type {
	u8 restore_state;
	u8 restore_status;
};

struct qmi_backup_mem_ready_type {
	u8 backup_addr_valid;
	u32 image_buffer_addr;
	u32 scratch_buffer_addr;
	u32 image_buffer_size;
	u32 scratch_buffer_size;
	u32 retry_timer;
};

struct qmi_restore_mem_ready_type {
	u8 restore_addr_valid;
	u32 image_buffer_addr;
	u32 scratch_buffer_addr;
	u32 image_buffer_size;
	u32 scratch_buffer_size;
};

struct qmi_backup_ind_reg_req {
	u8 need_qmi_backup_ind;
};
#define BACKUP_IND_REG_REQ_MAX_MSG_LEN 4

struct qmi_backup_ind_reg_resp {
	struct qmi_response_type_v01 resp;
};
#define BACKUP_IND_REG_RESP_MAX_MSG_LEN 7

struct qmi_restore_ind_reg_req {
	u8 need_qmi_restore_ind;
};
#define RESTORE_IND_REG_REQ_MAX_MSG_LEN 4

struct qmi_restore_ind_reg_resp {
	struct qmi_response_type_v01 resp;
};
#define RESTORE_IND_REG_RESP_MAX_MSG_LEN 7

struct qmi_backup_ind {
	struct qmi_backup_ind_type backup_info;
};
#define BACKUP_IND_MAX_MSG_LEN 10

struct qmi_restore_ind {
	struct qmi_restore_ind_type restore_info;
};
#define RESTORE_IND_MAX_MSG_LEN 5

struct qmi_backup_mem_ready_req {
	struct qmi_backup_mem_ready_type backup_mem_ready_info;
};
#define BACKUP_MEM_READY_REQ_MAX_MSG_LEN 24

struct qmi_backup_mem_ready_resp {
	struct qmi_response_type_v01 resp;
};
#define BACKUP_MEM_READY_RESP_MAX_MSG_LEN 7

struct qmi_restore_mem_ready_req {
	struct qmi_restore_mem_ready_type restore_mem_ready_info;
};
#define RESTORE_MEM_READY_REQ_MAX_MSG_LEN 20

struct qmi_restore_mem_ready_resp {
	struct qmi_response_type_v01 resp;
};
#define RESTORE_MEM_READY_RESP_MAX_MSG_LEN 7

struct buffer_info {
	phys_addr_t paddr;
	void *vaddr;
	size_t total_size;
	size_t used_size;
	bool hyp_assigned_to_hlos;
};

struct qmi_info {
	struct sockaddr_qrtr s_addr;
	struct qmi_handle qmi_svc_handle;
	bool connected;
	const void *decoded_msg;
	struct work_struct qmi_client_work;
};

/**
 * struct subsys_backup - subsystem backup-restore device
 * @dev: Device pointer
 * @state: Used to denote the current state.
 * @img_buf: Buffer where the encrypted image is stored. HLOS will read/write
 *	     this buffer.
 * @scratch_buf: Scratch Buffer used by remote subsystem during the backup
 *		 or restore.
 * @qmi: Details of the QMI client on the kernel side.
 * @cdev: Device used by the Userspace to read/write the image buffer.
 * @open_count: Account open instances.
 */
struct subsys_backup {
	struct device *dev;
	enum process_state state;
	struct buffer_info img_buf;
	struct buffer_info scratch_buf;
	struct qmi_info qmi;
	struct work_struct request_handler_work;
	struct cdev cdev;
	enum notif_type last_notif_sent;
	enum qmi_backup_type backup_type;
	enum qmi_remote_status remote_status;
	struct device *sysfs_dev;
	atomic_t open_count;
};

static struct qmi_elem_info qmi_backup_ind_type_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_ind_type,
						backup_state),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_ind_type,
						backup_image_size),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_ind_type,
						qmi_backup_type),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_ind_type,
						backup_status),
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_restore_ind_type_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_restore_ind_type,
						restore_state),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_restore_ind_type,
						restore_status),
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_backup_mem_ready_type_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_mem_ready_type,
						backup_addr_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_mem_ready_type,
						image_buffer_addr),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_mem_ready_type,
						scratch_buffer_addr),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_mem_ready_type,
						image_buffer_size),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_mem_ready_type,
						scratch_buffer_size),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_backup_mem_ready_type,
						retry_timer),
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_restore_mem_ready_type_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_restore_mem_ready_type,
						restore_addr_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_restore_mem_ready_type,
						image_buffer_addr),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_restore_mem_ready_type,
						scratch_buffer_addr),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_restore_mem_ready_type,
						image_buffer_size),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_restore_mem_ready_type,
						scratch_buffer_size),
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_backup_ind_reg_req_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_backup_ind_reg_req,
						need_qmi_backup_ind),
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_backup_ind_reg_resp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_backup_ind_reg_resp,
						resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_restore_ind_reg_req_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_restore_ind_reg_req,
						need_qmi_restore_ind),
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_restore_ind_reg_resp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_restore_ind_reg_resp,
						resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_backup_ind_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_backup_ind_type),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_backup_ind,
						backup_info),
		.ei_array	= qmi_backup_ind_type_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_restore_ind_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_restore_ind_type),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_restore_ind,
						restore_info),
		.ei_array	= qmi_restore_ind_type_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_backup_mem_ready_req_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_backup_mem_ready_type),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_backup_mem_ready_req,
						backup_mem_ready_info),
		.ei_array	= qmi_backup_mem_ready_type_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_backup_mem_ready_resp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_backup_mem_ready_resp,
						resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_restore_mem_ready_req_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_restore_mem_ready_type),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_restore_mem_ready_req,
						restore_mem_ready_info),
		.ei_array	= qmi_restore_mem_ready_type_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_restore_mem_ready_resp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.is_array	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_restore_mem_ready_resp,
						resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.is_array	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

const char *backup_type_to_str(enum qmi_backup_type backup_type)
{
	switch (backup_type) {

	case (BACKUP_TYPE_FACTORY):
		return "BACKUP_TYPE_FACTORY";
	case (BACKUP_TYPE_RUNTIME):
		return "BACKUP_TYPE_RUNTIME";
	case (BACKUP_SOFTWARE_ROLL_BACK):
		return "BACKUP_SOFTWARE_ROLL_BACK";
	default:
		return NULL;
	}
}

const char *event_to_str(enum notif_type event)
{
	switch (event) {

	case (BACKUP_NOTIF_START):
		return "BACKUP_NOTIF_START";
	case (BACKUP_NOTIF_END):
		return "BACKUP_NOTIF_END";
	case (RESTORE_NOTIF_START):
		return "RESTORE_NOTIF_START";
	case (RESTORE_NOTIF_END):
		return "RESTORE_NOTIF_END";
	case (BACKUP_ALLOC_SUCCESS):
		return "BACKUP_ALLOC_SUCCESS";
	case (BACKUP_ALLOC_FAIL):
		return "BACKUP_ALLOC_FAIL";
	case (RESTORE_ALLOC_SUCCESS):
		return "RESTORE_ALLOC_SUCCESS";
	case (RESTORE_ALLOC_FAIL):
		return "RESTORE_ALLOC_FAIL";
	default:
		return NULL;
	}
}

const char *status_to_str(enum qmi_remote_status remote_status)
{
	switch (remote_status) {

	case (SUCCESS):
		return "SUCCESS";
	case (FAILED):
		return "FAILED";
	case (ADDR_VALID):
		return "ADDR_VALID";
	case (ADDR_INVALID):
		return "ADDR_INVALID";
	default:
		return NULL;
	}
}

static int hyp_assign_buffers(struct subsys_backup *backup_dev, int dest,
				int src)
{
	int ret;
	int src_vmids[1] = {src};
	int dest_vmids[1] = {dest};
	int dest_perms[1] = {PERM_READ|PERM_WRITE};

	if (dest == VMID_HLOS)
		dest_perms[0] |= PERM_EXEC;

	if ((dest == VMID_HLOS && backup_dev->img_buf.hyp_assigned_to_hlos &&
		backup_dev->scratch_buf.hyp_assigned_to_hlos) ||
		(dest == VMID_MSS_MSA &&
		!backup_dev->img_buf.hyp_assigned_to_hlos
		&& !backup_dev->scratch_buf.hyp_assigned_to_hlos))
		return 0;

	ret = hyp_assign_phys(backup_dev->img_buf.paddr,
			backup_dev->img_buf.total_size, src_vmids, 1,
			dest_vmids, dest_perms, 1);
	if (ret)
		goto error_hyp;

	ret = hyp_assign_phys(backup_dev->scratch_buf.paddr,
			backup_dev->scratch_buf.total_size, src_vmids, 1,
			dest_vmids, dest_perms, 1);
	if (ret && dest != VMID_HLOS) {
		ret = hyp_assign_phys(backup_dev->img_buf.paddr,
				backup_dev->img_buf.total_size, dest_vmids, 1,
				src_vmids, dest_perms, 1);
		goto error_hyp;
	}

	if (dest == VMID_HLOS) {
		backup_dev->img_buf.hyp_assigned_to_hlos = true;
		backup_dev->scratch_buf.hyp_assigned_to_hlos = true;
	} else {
		backup_dev->img_buf.hyp_assigned_to_hlos = false;
		backup_dev->scratch_buf.hyp_assigned_to_hlos = false;
	}

	return 0;

error_hyp:
	dev_err(backup_dev->dev, "%s: Failed: %d\n", __func__, ret);
	return ret;
}

static int allocate_buffers(struct subsys_backup *backup_dev)
{
	int ret = -1;
	dma_addr_t scratch_dma_addr, img_dma_addr;

	backup_dev->img_buf.vaddr = (void *) dma_alloc_coherent(backup_dev->dev,
				backup_dev->img_buf.total_size, &img_dma_addr,
				GFP_KERNEL);

	backup_dev->scratch_buf.vaddr = (void *)
				dma_alloc_coherent(backup_dev->dev,
				backup_dev->scratch_buf.total_size,
				&scratch_dma_addr, GFP_KERNEL);

	if (!backup_dev->img_buf.vaddr || !backup_dev->scratch_buf.vaddr)
		goto error;

	backup_dev->img_buf.paddr = img_dma_addr;
	backup_dev->scratch_buf.paddr = scratch_dma_addr;
	backup_dev->img_buf.hyp_assigned_to_hlos = true;
	backup_dev->scratch_buf.hyp_assigned_to_hlos = true;

	memset(backup_dev->img_buf.vaddr, 0, backup_dev->img_buf.total_size);
	memset(backup_dev->scratch_buf.vaddr, 0,
			backup_dev->scratch_buf.total_size);
	return 0;

error:
	dev_err(backup_dev->dev, "%s: Failed\n", __func__);

	if (backup_dev->img_buf.vaddr)
		dma_free_coherent(backup_dev->dev,
				backup_dev->img_buf.total_size,
				backup_dev->img_buf.vaddr, img_dma_addr);
	if (backup_dev->scratch_buf.vaddr)
		dma_free_coherent(backup_dev->dev,
				backup_dev->scratch_buf.total_size,
				backup_dev->scratch_buf.vaddr,
				scratch_dma_addr);
	backup_dev->img_buf.vaddr = NULL;
	backup_dev->scratch_buf.vaddr = NULL;
	return ret;
}

static int free_buffers(struct subsys_backup *backup_dev)
{
	if (backup_dev->img_buf.vaddr)
		dma_free_coherent(backup_dev->dev,
			backup_dev->img_buf.total_size,
			backup_dev->img_buf.vaddr, backup_dev->img_buf.paddr);

	if (backup_dev->scratch_buf.vaddr)
		dma_free_coherent(backup_dev->dev,
			backup_dev->scratch_buf.total_size,
			backup_dev->scratch_buf.vaddr,
			backup_dev->scratch_buf.paddr);

	backup_dev->img_buf.vaddr = NULL;
	backup_dev->scratch_buf.vaddr = NULL;
	return 0;
}

static int subsys_qmi_send_request(struct subsys_backup *backup_dev,
			int msg_id, size_t len, struct qmi_elem_info *req_ei,
			const void *req_data, struct qmi_elem_info *resp_ei,
			void *resp_data)
{
	int ret;
	struct qmi_txn txn;
	struct qmi_response_type_v01 *resp;

	ret = qmi_txn_init(&backup_dev->qmi.qmi_svc_handle, &txn, resp_ei,
				resp_data);
	if (ret < 0) {
		dev_err(backup_dev->dev, "%s: Failed to init txn: %d\n",
				__func__, ret);
		goto out;
	}

	ret = qmi_send_request(&backup_dev->qmi.qmi_svc_handle,
			&backup_dev->qmi.s_addr, &txn, msg_id, len, req_ei,
			req_data);
	if (ret) {
		qmi_txn_cancel(&txn);
		goto out;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0) {
		dev_err(backup_dev->dev, "%s: Response wait failed: %d\n",
				__func__, ret);
		goto out;
	}

	resp = (struct qmi_response_type_v01 *)resp_data;

	if (resp->result != QMI_RESULT_SUCCESS_V01) {
		ret = -resp->result;
		goto out;
	}

	return 0;

out:
	return ret;
}

static int backup_notify_remote(struct subsys_backup *backup_dev,
				enum notif_type type)
{
	struct qmi_backup_mem_ready_req *req;
	struct qmi_backup_mem_ready_resp *resp;
	struct qmi_backup_mem_ready_type *data;
	int ret;

	req = devm_kzalloc(backup_dev->dev,
			sizeof(struct qmi_backup_mem_ready_req),
			GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	resp = devm_kzalloc(backup_dev->dev,
			sizeof(struct qmi_backup_mem_ready_resp),
			GFP_KERNEL);
	if (!resp) {
		devm_kfree(backup_dev->dev, req);
		return -ENOMEM;
	}

	data = &req->backup_mem_ready_info;
	if (type == BACKUP_ALLOC_SUCCESS) {
		data->backup_addr_valid = ADDR_VALID;
		data->image_buffer_addr = backup_dev->img_buf.paddr;
		data->scratch_buffer_addr = backup_dev->scratch_buf.paddr;
		data->image_buffer_size = backup_dev->img_buf.total_size;
		data->scratch_buffer_size = backup_dev->scratch_buf.total_size;
	} else if (type == BACKUP_ALLOC_FAIL) {
		data->backup_addr_valid = ADDR_INVALID;
		data->retry_timer = 10;
	}

	ret = subsys_qmi_send_request(backup_dev,
			QMI_BACKUP_MEM_READY_REQ_MSG_ID,
			BACKUP_MEM_READY_REQ_MAX_MSG_LEN,
			qmi_backup_mem_ready_req_ei, req,
			qmi_backup_mem_ready_resp_ei, resp);
	if (ret) {
		dev_err(backup_dev->dev, "%s: Failed QMI request: %d",
				__func__, ret);
		goto out;
	}

	return 0;

out:
	devm_kfree(backup_dev->dev, req);
	devm_kfree(backup_dev->dev, resp);
	return ret;
}

static int restore_notify_remote(struct subsys_backup *backup_dev,
				enum notif_type type)
{
	struct qmi_restore_mem_ready_req *req;
	struct qmi_restore_mem_ready_resp *resp;
	struct qmi_restore_mem_ready_type *data;
	int ret;

	req = devm_kzalloc(backup_dev->dev,
			sizeof(struct qmi_restore_mem_ready_req),
			GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	resp = devm_kzalloc(backup_dev->dev,
			sizeof(struct qmi_backup_mem_ready_resp),
			GFP_KERNEL);
	if (!resp) {
		devm_kfree(backup_dev->dev, req);
		return -ENOMEM;
	}

	data = &req->restore_mem_ready_info;
	if (type == RESTORE_ALLOC_SUCCESS) {
		data->restore_addr_valid = ADDR_VALID;
		data->image_buffer_addr = backup_dev->img_buf.paddr;
		data->scratch_buffer_addr = backup_dev->scratch_buf.paddr;
		data->image_buffer_size = backup_dev->img_buf.total_size;
		data->scratch_buffer_size = backup_dev->scratch_buf.total_size;
	} else if (type == RESTORE_ALLOC_FAIL) {
		dev_warn(backup_dev->dev, "%s: Remote notification skipped\n",
				__func__);
		goto exit;
	}

	ret = subsys_qmi_send_request(backup_dev,
			QMI_RESTORE_MEM_READY_REQ_MSG_ID,
			RESTORE_MEM_READY_REQ_MAX_MSG_LEN,
			qmi_restore_mem_ready_req_ei, req,
			qmi_restore_mem_ready_resp_ei, resp);
	if (ret) {
		dev_err(backup_dev->dev, "%s: Failed QMI request: %d",
				__func__, ret);
		goto exit;
	}

	return 0;

exit:
	devm_kfree(backup_dev->dev, req);
	devm_kfree(backup_dev->dev, resp);
	return ret;
}

static void notify_userspace(struct subsys_backup *backup_dev,
				enum notif_type type)
{
	char *info[2] = {NULL, NULL};

	backup_dev->last_notif_sent = type;
	switch (type) {

	case BACKUP_NOTIF_START:
		info[0] = "EVENT=BACKUP_NOTIF_START";
		break;
	case BACKUP_NOTIF_END:
		info[0] = "EVENT=BACKUP_NOTIF_END";
		break;
	case RESTORE_NOTIF_START:
		info[0] = "EVENT=RESTORE_NOTIF_START";
		break;
	case RESTORE_NOTIF_END:
		info[0] = "EVENT=RESTORE_NOTIF_END";
		break;
	case BACKUP_ALLOC_SUCCESS:
		info[0] = "EVENT=BACKUP_ALLOC_SUCCESS";
		break;
	case BACKUP_ALLOC_FAIL:
		info[0] = "EVENT=BACKUP_ALLOC_FAIL";
		break;
	case RESTORE_ALLOC_SUCCESS:
		info[0] = "EVENT=RESTORE_ALLOC_SUCCESS";
		break;
	case RESTORE_ALLOC_FAIL:
		info[0] = "EVENT=RESTORE_ALLOC_FAIL";
		break;
	}

	kobject_uevent_env(&backup_dev->dev->kobj, KOBJ_CHANGE, info);
}

static int subsys_backup_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct subsys_backup *backup_dev;
	int ret = 0;

	if (!dev)
		return -ENODEV;

	backup_dev = dev_get_drvdata(dev);
	if (!backup_dev)
		return -ENODEV;

	ret = add_uevent_var(env, "EVENT=%s",
			event_to_str(backup_dev->last_notif_sent));
	if (ret)
		dev_err(backup_dev->dev, "Failed add_uevent_var: %d\n", ret);

	ret = add_uevent_var(env, "BACKUP_TYPE=%s",
			backup_type_to_str(backup_dev->backup_type));
	if (ret)
		dev_err(backup_dev->dev, "Failed add_uevent_var: %d\n", ret);

	ret = add_uevent_var(env, "REMOTE_STATUS=%s",
			status_to_str(backup_dev->remote_status));
	if (ret)
		dev_err(backup_dev->dev, "Failed add_uevent_var: %d\n", ret);

	return ret;
}

static void request_handler_worker(struct work_struct *work)
{
	struct subsys_backup *backup_dev;
	struct qmi_backup_ind_type *ind;
	int ret;

	backup_dev = container_of(work, struct subsys_backup,
				request_handler_work);

	switch (backup_dev->state) {

	case BACKUP_START:
		notify_userspace(backup_dev, BACKUP_NOTIF_START);
		ind = (struct qmi_backup_ind_type *)backup_dev->qmi.decoded_msg;
		backup_dev->backup_type = ind->qmi_backup_type;
		if (allocate_buffers(backup_dev) ||
				hyp_assign_buffers(backup_dev, VMID_MSS_MSA,
				VMID_HLOS)) {
			free_buffers(backup_dev);
			notify_userspace(backup_dev, BACKUP_ALLOC_FAIL);
			ret = backup_notify_remote(backup_dev,
							BACKUP_ALLOC_FAIL);
			if (!ret)
				dev_err(backup_dev->dev,
					"%s: Remote notif failed %d\n",
					__func__, ret);
			backup_dev->state = IDLE;
			break;
		}
		notify_userspace(backup_dev, BACKUP_ALLOC_SUCCESS);
		backup_notify_remote(backup_dev, BACKUP_ALLOC_SUCCESS);
		break;

	case BACKUP_END:
		ind =
		(struct qmi_backup_ind_type *)backup_dev->qmi.decoded_msg;
		if (ind->backup_image_size > backup_dev->img_buf.total_size) {
			dev_err(backup_dev->dev, "%s: Invalid image size\n",
					__func__);
			free_buffers(backup_dev);
			backup_dev->state = IDLE;
			break;
		}
		backup_dev->img_buf.used_size = ind->backup_image_size;
		ret = hyp_assign_buffers(backup_dev, VMID_HLOS, VMID_MSS_MSA);
		if (ret) {
			dev_err(backup_dev->dev,
				"%s: Hyp_assingn to HLOS failed: %d\n",
				__func__, ret);
			free_buffers(backup_dev);
			backup_dev->state = IDLE;
			break;
		}
		notify_userspace(backup_dev, BACKUP_NOTIF_END);
		break;

	case RESTORE_START:
		notify_userspace(backup_dev, RESTORE_NOTIF_START);
		ret = allocate_buffers(backup_dev);
		if (ret) {
			dev_err(backup_dev->dev,
					"Error allocation of buffers\n");
			notify_userspace(backup_dev, RESTORE_ALLOC_FAIL);
			backup_dev->state = IDLE;
			break;
		}
		notify_userspace(backup_dev, RESTORE_ALLOC_SUCCESS);
		break;

	case RESTORE_END:
		notify_userspace(backup_dev, RESTORE_NOTIF_END);
		ret = hyp_assign_buffers(backup_dev, VMID_HLOS, VMID_MSS_MSA);
		if (ret)
			dev_err(backup_dev->dev,
				"%s: Hyp_assingn to HLOS Failed: %d\n",
				__func__, ret);
		else
			free_buffers(backup_dev);
		backup_dev->state = IDLE;
		break;

	default:
		dev_err(backup_dev->dev, "%s: Invalid state\n", __func__);
		break;
	}
}

static int is_valid_indication(struct subsys_backup *dev, void *ind,
							 int restore)
{
	struct qmi_backup_ind_type *backup_ind;
	struct qmi_restore_ind_type *restore_ind;

	if (restore) {
		/* Is backup in progress? */
		if (dev->state == BACKUP_START || dev->state == BACKUP_END)
			return 0;
		restore_ind = (struct qmi_restore_ind_type *)ind;
		/* Duplicate start indication */
		if (dev->state == RESTORE_START &&
				restore_ind->restore_state == START)
			return 0;
		/* Duplicate end indication */
		if (dev->state == RESTORE_END &&
				restore_ind->restore_state == END)
			return 0;
	} else {
		/* Is restore in progress? */
		if (dev->state == RESTORE_START || dev->state == RESTORE_END)
			return 0;
		backup_ind = (struct qmi_backup_ind_type *)ind;
		/* Duplicate start indication */
		if (dev->state == BACKUP_START &&
				backup_ind->backup_state == START)
			return 0;
		/* Duplicate end indication */
		if (dev->state == BACKUP_END &&
				backup_ind->backup_state == END)
			return 0;
	}
	return 1;
}

static void backup_notif_handler(struct qmi_handle *handle,
	struct sockaddr_qrtr *sq, struct qmi_txn *txn, const void *decoded_msg)
{
	struct qmi_backup_ind_type *ind;
	struct subsys_backup *backup_dev;
	struct qmi_info *qmi;

	qmi = container_of(handle, struct qmi_info, qmi_svc_handle);
	backup_dev = container_of(qmi, struct subsys_backup, qmi);

	if (atomic_read(&backup_dev->open_count) == 0) {
		dev_err(backup_dev->dev, "%s: No active users\n", __func__);
		return;
	}

	ind = (struct qmi_backup_ind_type *)decoded_msg;
	if (!is_valid_indication(backup_dev, (void *)ind, 0)) {
		dev_err(backup_dev->dev, "%s: Error: Spurious request\n",
				__func__);
		return;
	}

	if (ind->backup_state == START) {
		backup_dev->state = BACKUP_START;
	} else if (ind->backup_state == END) {
		backup_dev->state = BACKUP_END;
	} else {
		dev_err(backup_dev->dev, "%s: Invalid request\n", __func__);
		return;
	}

	backup_dev->qmi.decoded_msg = devm_kzalloc(backup_dev->dev,
					sizeof(*decoded_msg), GFP_KERNEL);
	if (!backup_dev->qmi.decoded_msg) {
		dev_err(backup_dev->dev, "%s: Failed to allocate memory\n",
				__func__);
		return;
	}

	memcpy((void *)backup_dev->qmi.decoded_msg, decoded_msg,
			sizeof(*decoded_msg));
	queue_work(system_wq, &backup_dev->request_handler_work);
}

static void restore_notif_handler(struct qmi_handle *handle,
	struct sockaddr_qrtr *sq, struct qmi_txn *txn, const void *decoded_msg)
{
	struct qmi_restore_ind_type *ind;
	struct subsys_backup *backup_dev;
	struct qmi_info *qmi;

	qmi = container_of(handle, struct qmi_info, qmi_svc_handle);
	backup_dev = container_of(qmi, struct subsys_backup, qmi);

	if (atomic_read(&backup_dev->open_count) == 0) {
		dev_err(backup_dev->dev, "%s: No active users\n", __func__);
		return;
	}

	ind = (struct qmi_restore_ind_type *)decoded_msg;
	if (!is_valid_indication(backup_dev, (void *)ind, 1)) {
		dev_err(backup_dev->dev, "%s: Error: Spurious request\n",
				__func__);
		return;
	}

	if (ind->restore_state == START) {
		backup_dev->state = RESTORE_START;
	} else if (ind->restore_state == END) {
		backup_dev->state = RESTORE_END;
		backup_dev->remote_status = ind->restore_status;
	} else {
		dev_err(backup_dev->dev, "%s: Invalid request\n", __func__);
		return;
	}

	backup_dev->qmi.decoded_msg = devm_kzalloc(backup_dev->dev,
					sizeof(*decoded_msg), GFP_KERNEL);
	if (!backup_dev->qmi.decoded_msg) {
		dev_err(backup_dev->dev, "%s: Failed to allocate memory\n",
				__func__);
		return;
	}

	memcpy((void *)backup_dev->qmi.decoded_msg, decoded_msg,
		sizeof(*decoded_msg));
	queue_work(system_wq, &backup_dev->request_handler_work);
}

static struct qmi_msg_handler qmi_backup_restore_handlers[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_BACKUP_IND_MSG_ID,
		.ei = qmi_backup_ind_ei,
		.decoded_size = BACKUP_IND_MAX_MSG_LEN,
		.fn = backup_notif_handler,
	},
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_RESTORE_IND_MSG_ID,
		.ei = qmi_restore_ind_ei,
		.decoded_size = RESTORE_IND_MAX_MSG_LEN,
		.fn = restore_notif_handler,
	},
};

static int register_for_backup_restore_notif(struct subsys_backup *backup_dev)
{
	struct qmi_backup_ind_reg_req backup_req;
	struct qmi_backup_ind_reg_resp backup_req_resp;
	struct qmi_restore_ind_reg_req restore_req;
	struct qmi_restore_ind_reg_resp restore_req_resp;
	int ret;

	if (!backup_dev->qmi.connected) {
		dev_err(backup_dev->dev, "%s: Not Connected to QMI server\n",
				__func__);
		return -EINVAL;
	}

	restore_req.need_qmi_restore_ind = true;
	ret = subsys_qmi_send_request(backup_dev,
			QMI_RESTORE_IND_REG_REQ_MSG_ID,
			RESTORE_IND_REG_REQ_MAX_MSG_LEN,
			qmi_restore_ind_reg_req_ei, &restore_req,
			qmi_restore_ind_reg_resp_ei, &restore_req_resp);
	if (ret) {
		dev_err(backup_dev->dev, "%s: Failed: Restore notif req: %d\n",
				__func__, ret);
		return ret;
	}

	backup_req.need_qmi_backup_ind = true;
	ret = subsys_qmi_send_request(backup_dev,
			QMI_BACKUP_IND_REG_REQ_MSG_ID,
			BACKUP_IND_REG_REQ_MAX_MSG_LEN,
			qmi_backup_ind_reg_req_ei, &backup_req,
			qmi_backup_ind_reg_resp_ei, &backup_req_resp);
	if (ret) {
		dev_err(backup_dev->dev, "%s: Failed: Backup notif req: %d\n",
				__func__, ret);
		return ret;
	}

	return 0;
}

static int subsys_backup_new_server(struct qmi_handle *handle,
				struct qmi_service *svc)
{
	struct subsys_backup *backup_dev;
	struct qmi_info *qmi;

	qmi = container_of(handle, struct qmi_info, qmi_svc_handle);
	backup_dev = container_of(qmi, struct subsys_backup, qmi);

	backup_dev->qmi.s_addr.sq_family = AF_QIPCRTR;
	backup_dev->qmi.s_addr.sq_node = svc->node;
	backup_dev->qmi.s_addr.sq_port = svc->port;
	backup_dev->qmi.connected = true;

	return register_for_backup_restore_notif(backup_dev);
}

static void subsys_backup_del_server(struct qmi_handle *handle,
				struct qmi_service *svc)
{
	struct subsys_backup *backup_dev;
	struct qmi_info *qmi;

	qmi = container_of(handle, struct qmi_info, qmi_svc_handle);
	backup_dev = container_of(qmi, struct subsys_backup, qmi);

	backup_dev->qmi.connected = false;

	hyp_assign_buffers(backup_dev, VMID_HLOS, VMID_MSS_MSA);
	free_buffers(backup_dev);
}

static struct qmi_ops server_ops = {
	.new_server = subsys_backup_new_server,
	.del_server = subsys_backup_del_server
};

static void qmi_client_worker(struct work_struct *work)
{
	int ret;
	struct qmi_info *qmi;
	struct subsys_backup *backup_dev;

	qmi = container_of(work, struct qmi_info, qmi_client_work);
	backup_dev = container_of(qmi, struct subsys_backup, qmi);

	ret = qmi_handle_init(&backup_dev->qmi.qmi_svc_handle,
			200, &server_ops,
			qmi_backup_restore_handlers);
	if (ret) {
		dev_err(backup_dev->dev, "%s: Failed qmi_handle_init\n",
				__func__);
		return;
	}

	ret = qmi_add_lookup(&backup_dev->qmi.qmi_svc_handle,
			SUBSYS_BACKUP_SVC_ID, SUBSYS_BACKUP_SVC_VERS,
			SUBSYS_BACKUP_SVC_INS_ID);
	if (ret) {
		dev_err(backup_dev->dev, "%s: Failed qmi_add_lookup: %d\n",
				__func__, ret);
		qmi_handle_release(&backup_dev->qmi.qmi_svc_handle);
		return;
	}

	INIT_WORK(&backup_dev->request_handler_work, request_handler_worker);

	dev_info(backup_dev->dev, "%s: Done init\n", __func__);
}

static int backup_buffer_open(struct inode *inodep, struct file *filep)
{
	struct subsys_backup *backup_dev = container_of(inodep->i_cdev,
					struct subsys_backup, cdev);
	if (atomic_inc_return(&backup_dev->open_count) != 1) {
		dev_err(backup_dev->dev,
				"Multiple instances of open  not allowed\n");
		atomic_dec(&backup_dev->open_count);
		return -EBUSY;
	}
	filep->private_data = backup_dev;
	return 0;
}

static ssize_t backup_buffer_read(struct file *filp, char __user *buf,
		size_t size, loff_t *offp)
{
	struct subsys_backup *backup_dev = filp->private_data;
	size_t ret;

	if (backup_dev->state != BACKUP_END || !backup_dev->img_buf.vaddr ||
		!backup_dev->img_buf.hyp_assigned_to_hlos) {
		dev_err(backup_dev->dev, "%s: Invalid Operation\n", __func__);
		return 0;
	}

	ret = simple_read_from_buffer(buf, size, offp,
			backup_dev->img_buf.vaddr,
			backup_dev->img_buf.used_size);
	if (ret < 0)
		dev_err(backup_dev->dev, "%s: Failed: %d\n", __func__, ret);
	else if (ret < size)
		free_buffers(backup_dev);

	return ret;
}

static ssize_t backup_buffer_write(struct file *filp, const char __user *buf,
		size_t size, loff_t *offp)
{
	struct subsys_backup *backup_dev = filp->private_data;

	if (backup_dev->state != RESTORE_START) {
		dev_err(backup_dev->dev, "%s: Restore not started\n", __func__);
		return 0;
	} else if (!backup_dev->img_buf.hyp_assigned_to_hlos) {
		dev_err(backup_dev->dev, "%s: Not hyp_assinged to HLOS\n",
				__func__);
		return 0;
	}


	return simple_write_to_buffer(backup_dev->img_buf.vaddr,
			backup_dev->img_buf.total_size, offp, buf, size);
}

static int backup_buffer_flush(struct file *filp, fl_owner_t id)
{
	int ret;
	struct subsys_backup *backup_dev = filp->private_data;

	if (backup_dev->state == IDLE &&
		backup_dev->img_buf.hyp_assigned_to_hlos)
		return 0;

	if (backup_dev->state != RESTORE_START || !backup_dev->img_buf.vaddr) {
		dev_err(backup_dev->dev, "%s: Invalid operation\n", __func__);
		return -EBUSY;
	}


	ret = hyp_assign_buffers(backup_dev, VMID_MSS_MSA, VMID_HLOS);
	if (ret) {
		dev_err(backup_dev->dev, "%s: Failed hyp_assign to MPSS: %d\n",
				__func__, ret);
		return 0;
	}

	ret = restore_notify_remote(backup_dev, RESTORE_ALLOC_SUCCESS);
	if (ret)
		dev_err(backup_dev->dev, "%s: Remote notif failed: %d\n",
				__func__, ret);
	return 0;
}

static int backup_buffer_release(struct inode *inodep, struct file *filep)
{
	struct subsys_backup *backup_dev = container_of(inodep->i_cdev,
					struct subsys_backup, cdev);
	int retry = 0, ret;

	/*
	 * In case the remote subsystem is actively using the shared memory,
	 * wait for it to release. Hyp_assing to HLOS before the release might
	 * fail. Retry hyp_assign for few times before it succeeds.
	 */
	do {
		ret = hyp_assign_buffers(backup_dev, VMID_HLOS, VMID_MSS_MSA);
		if (ret == 0)
			break;
		usleep_range(100000, 200000);
		BUG_ON(retry == 10);
		retry++;
	} while (ret);

	free_buffers(backup_dev);
	backup_dev->state = IDLE;
	atomic_dec(&backup_dev->open_count);
	return 0;
}

static const struct file_operations backup_buffer_fops = {
	.owner	= THIS_MODULE,
	.open	= backup_buffer_open,
	.read	= backup_buffer_read,
	.write	= backup_buffer_write,
	.flush	= backup_buffer_flush,
	.release = backup_buffer_release,
};

static int subsys_backup_init_device(struct platform_device *pdev,
					struct subsys_backup *backup_dev)
{
	int ret;
	dev_t dev;
	struct class *class;
	struct device *device;

	ret = alloc_chrdev_region(&dev, 0, 1, "subsys-backup");
	if (ret < 0) {
		dev_err(backup_dev->dev, "alloc_chrdev_region failed: %d\n",
					ret);
		return ret;
	}

	cdev_init(&backup_dev->cdev, &backup_buffer_fops);
	backup_dev->cdev.owner = THIS_MODULE;

	/* As of now driver supports only one device */
	ret = cdev_add(&backup_dev->cdev, dev, 1);
	if (ret < 0) {
		dev_err(backup_dev->dev, "cdev_add failed: %d\n", ret);
		goto cdev_add_err;
	}

	class = class_create(THIS_MODULE, "subsys-backup");
	if (IS_ERR(class)) {
		ret = PTR_ERR(class);
		dev_err(backup_dev->dev, "class_create failed: %d\n", ret);
		goto class_create_err;
	}

	device = device_create(class, NULL, dev, backup_dev, "subsys-backup");
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		dev_err(backup_dev->dev, "device_create failed: %d\n", ret);
		goto device_create_err;
	}
	backup_dev->sysfs_dev = backup_dev->dev;
	dev_set_drvdata(backup_dev->sysfs_dev, backup_dev);

	class->dev_uevent = subsys_backup_uevent;
	return 0;

device_create_err:
	class_destroy(class);
class_create_err:
	cdev_del(&backup_dev->cdev);
cdev_add_err:
	unregister_chrdev_region(dev, 1);
	return ret;
}

static int subsys_backup_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct subsys_backup *backup_dev;
	u32 buf_size;

	backup_dev = devm_kzalloc(&pdev->dev, sizeof(struct subsys_backup),
					GFP_KERNEL);
	if (!backup_dev)
		return -ENOMEM;

	if (of_property_read_u32(pdev->dev.of_node, "qcom,buf-size",
			&buf_size)) {
		dev_err(&pdev->dev, "Could not find property qcom,buf-size\n");
		goto buf_size_err;
	}

	backup_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, backup_dev);

	backup_dev->img_buf.total_size = buf_size;
	backup_dev->scratch_buf.total_size = buf_size;

	ret = subsys_backup_init_device(pdev, backup_dev);
	if (ret < 0)
		goto buf_size_err;

	INIT_WORK(&backup_dev->qmi.qmi_client_work, qmi_client_worker);
	queue_work(system_wq, &backup_dev->qmi.qmi_client_work);

	return 0;

buf_size_err:
	kfree(backup_dev);
	return ret;
}

static int subsys_backup_driver_remove(struct platform_device *pdev)
{
	struct subsys_backup *backup_dev;

	backup_dev = platform_get_drvdata(pdev);
	qmi_handle_release(&backup_dev->qmi.qmi_svc_handle);
	kfree(&backup_dev->qmi.qmi_svc_handle);
	kfree(backup_dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id subsys_backup_match_table[] = {
	{
		.compatible = "qcom,subsys-backup",
	},
	{}
};

static struct platform_driver subsys_backup_pdriver = {
	.probe	   = subsys_backup_driver_probe,
	.remove	  = subsys_backup_driver_remove,
	.driver = {
		.name   = "subsys_backup",
		.owner  = THIS_MODULE,
		.of_match_table = subsys_backup_match_table,
	},
};

module_platform_driver(subsys_backup_pdriver);

MODULE_DESCRIPTION("Subsystem Backup-Restore driver");
MODULE_LICENSE("GPL v2");
