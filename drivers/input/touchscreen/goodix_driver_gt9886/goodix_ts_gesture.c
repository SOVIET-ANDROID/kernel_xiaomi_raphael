/*
 * Goodix GTX5 Gesture Dirver
 *
 * Copyright (C) 2015 - 2016 Goodix, Inc.
 * Authors:  Wang Yafei <wangyafei@goodix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/input/mt.h>
#include <asm/atomic.h>
#include "goodix_ts_core.h"

#define GSX_REG_GESTURE_DATA			0x4100
#define GSX_REG_GESTURE				0x6F68

#define GSX_GESTURE_CMD				0x08

#define QUERYBIT(longlong, bit) (!!(longlong[bit/8] & (1 << bit%8)))

extern int goodix_i2c_write(struct goodix_ts_device *dev, unsigned int reg, unsigned char *data, unsigned int len);
extern int goodix_i2c_read(struct goodix_ts_device *dev, unsigned int reg, unsigned char *data, unsigned int len);
static int goodix_set_suspend_func(struct goodix_ts_core *core_data);
static int goodix_wakeup_and_set_suspend_func(struct goodix_ts_core *core_data);




#define GSX_KEY_DATA_LEN	37
#define GSX_GESTURE_TYPE_LEN	32
static int FP_Event_Gesture;

/*
 * struct gesture_module - gesture module data
 * @registered: module register state
 * @sysfs_node_created: sysfs node state
 * @gesture_type: store valied gesture type,each bit stand for a gesture
 * @gesture_ts_cmd: gesture command data
*/
struct gesture_module {
	atomic_t registered;
	unsigned int kobj_initialized;
	rwlock_t rwlock;
	unsigned char gesture_type[GSX_GESTURE_TYPE_LEN];
	struct goodix_ext_module module;
	struct goodix_ts_cmd cmd;
};

extern struct goodix_module goodix_modules; /*declaration at goodix_ts_core.c*/

static struct gesture_module *gsx_gesture; /*allocated in gesture init module*/


/**
 * gsx_gesture_type_show - show valid gesture type
 *
 * @module: pointer to goodix_ext_module struct
 * @buf: pointer to output buffer
 * Returns >=0 - succeed,< 0 - failed
 */
static ssize_t gsx_gesture_type_show(struct goodix_ext_module *module,
				char *buf)
{
	int count = 0, i, ret = 0;
	unsigned char *type;

	if (atomic_read(&gsx_gesture->registered) != 1) {
		ts_info("Gesture module not register!");
		return -EPERM;
	}
	type = kzalloc(256, GFP_KERNEL);
	if (!type)
		return -ENOMEM;
	read_lock(&gsx_gesture->rwlock);
	for (i = 0; i < 256; i++) {
		if (QUERYBIT(gsx_gesture->gesture_type, i)) {
			type[count] = i;
			count++;
		}
	}
	type[count] = '\0';
	if (count > 0) {
		/* TODO 这里使用scnprintf需要确认一下是否有效 */
		ret = scnprintf(buf, PAGE_SIZE, "%s", type);
	}
	read_unlock(&gsx_gesture->rwlock);

	kfree(type);
	return ret;
}

/**
 * gsx_gesture_type_store - set vailed gesture
 *
 * @module: pointer to goodix_ext_module struct
 * @buf: pointer to valid gesture type
 * @count: length of buf
 * Returns >0 - valid gestures, < 0 - failed
 */
static ssize_t gsx_gesture_type_store(struct goodix_ext_module *module,
		const char *buf, size_t count)
{
	int i;

	if (count <= 0 || count > 256 || buf == NULL) {
		ts_err("Parameter error");
		return -EINVAL;
	}

	write_lock(&gsx_gesture->rwlock);
	memset(gsx_gesture->gesture_type, 0, GSX_GESTURE_TYPE_LEN);
	for (i = 0; i < count; i++)
		gsx_gesture->gesture_type[buf[i]/8] |= (0x1 << buf[i]%8);
	write_unlock(&gsx_gesture->rwlock);

	return count;
}

static ssize_t gsx_gesture_enable_show(struct goodix_ext_module *module,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&gsx_gesture->registered));
}

static ssize_t gsx_gesture_enable_store(struct goodix_ext_module *module,
		const char *buf, size_t count)
{
	unsigned int tmp;
	int ret;

	if (sscanf(buf, "%u", &tmp) != 1) {
		ts_info("Parameter illegal");
		return -EINVAL;
	}
	ts_debug("Tmp value =%d", tmp);

	if (tmp == 1) {
		if (atomic_read(&gsx_gesture->registered)) {
			ts_debug("Gesture module has aready registered");
			return count;
		}
		ret = goodix_register_ext_module(&gsx_gesture->module);
		if (!ret) {
			ts_info("Gesture module registered!");
			atomic_set(&gsx_gesture->registered, 1);
		} else {
			atomic_set(&gsx_gesture->registered, 0);
			ts_err("Gesture module register failed");
		}
	} else if (tmp == 0) {
		if (!atomic_read(&gsx_gesture->registered)) {
			ts_debug("Gesture module has aready unregistered");
			return count;
		}
		ts_debug("Start unregistered gesture module");
		ret = goodix_unregister_ext_module(&gsx_gesture->module);
		if (!ret) {
			atomic_set(&gsx_gesture->registered, 0);
			ts_info("Gesture module unregistered success");
		} else {
			atomic_set(&gsx_gesture->registered, 1);
			ts_info("Gesture module unregistered failed");
		}
	} else {
		ts_err("Parameter error!");
		return -EINVAL;
	}
	return count;
}

int goodix_gesture_enable(bool enable)
{
	int ret = 0;

	if (enable == true) {
		if (atomic_read(&gsx_gesture->registered)) {
			ts_debug("Gesture module has aready registered");
			return 0;
		}
		ret = goodix_register_ext_module(&gsx_gesture->module);
		if (!ret) {
			ts_info("Gesture module registered!");
			atomic_set(&gsx_gesture->registered, 1);
		} else {
			atomic_set(&gsx_gesture->registered, 0);
			ts_err("Gesture module register failed");
		}
	} else {
		if (!atomic_read(&gsx_gesture->registered)) {
			ts_debug("Gesture module has aready unregistered");
			return 0;
		}
		ts_debug("Start unregistered gesture module");
		ret = goodix_unregister_ext_module(&gsx_gesture->module);
		if (!ret) {
			atomic_set(&gsx_gesture->registered, 0);
			ts_info("Gesture module unregistered success");
		} else {
			atomic_set(&gsx_gesture->registered, 1);
			ts_info("Gesture module unregistered failed");
		}
	}

	return 0;
}

int goodix_sync_ic_stat(struct goodix_ts_core *core_data)
{
	int tp_stat;
	int ret = 0;

	if (!core_data) {
		ts_err("parameter illegal");
	}

	mutex_lock(&core_data->work_stat);
	tp_stat = atomic_read(&core_data->suspend_stat);
	switch (tp_stat) {
	case TP_SLEEP:
		goto fromsleep;
	case TP_NO_SUSPEND:
		goto exit;
	case TP_GESTURE_DBCLK:
	case TP_GESTURE_FOD:
	case TP_GESTURE_DBCLK_FOD:
		goto sync;
	}

sync:
	ret = goodix_set_suspend_func(core_data);
	if (ret < 0)
		ts_err("set suspend function failed");
	goto exit;

fromsleep:
	ret = goodix_wakeup_and_set_suspend_func(core_data);
	if (ret < 0)
		ts_err("set suspend function failed");
	goto exit;

exit:
	mutex_unlock(&core_data->work_stat);
	return ret;
}

int goodix_check_gesture_stat(bool enable)
{
	if (enable) {
		goodix_sync_ic_stat(goodix_core_data);
	}
	return 0;
}

const struct goodix_ext_attribute gesture_attrs[] = {
	__EXTMOD_ATTR(type, 0666, gsx_gesture_type_show,
		gsx_gesture_type_store),
	__EXTMOD_ATTR(enable, 0666, gsx_gesture_enable_show,
		gsx_gesture_enable_store)
};

static int gsx_gesture_init(struct goodix_ts_core *core_data,
		struct goodix_ext_module *module)
{
	int i, ret;
	struct goodix_ts_device *ts_dev = core_data->ts_dev;

	if (!core_data || !ts_dev->hw_ops->write || !ts_dev->hw_ops->read) {
		ts_err("Register gesture module failed, ts_core unsupported");
		goto exit_gesture_init;
	}

	gsx_gesture->cmd.cmd_reg = GSX_REG_GESTURE;
	gsx_gesture->cmd.length = 3;
	gsx_gesture->cmd.cmds[0] = GSX_GESTURE_CMD;
	gsx_gesture->cmd.cmds[1] = 0x0;
	gsx_gesture->cmd.cmds[2] = 0 - GSX_GESTURE_CMD;
	gsx_gesture->cmd.initialized = 1;

	memset(gsx_gesture->gesture_type, 0, GSX_GESTURE_TYPE_LEN);

	ts_debug("Set gesture type manually");
	memset(gsx_gesture->gesture_type, 0xff, GSX_GESTURE_TYPE_LEN);
	/*gsx_gesture->gesture_type[34/8] |= (0x1 << 34%8);*/	/* 0x22 double click */
	/*gsx_gesture->gesture_type[170/8] |= (0x1 << 170%8);*/	/* 0xaa up swip */
	/*gsx_gesture->gesture_type[187/8] |= (0x1 << 187%8);*/	/* 0xbb right swip */
	/*gsx_gesture->gesture_type[171/8] |= (0x1 << 171%8);*/	/* 0xab down swip */
	/*gsx_gesture->gesture_type[186/8] |= (0x1 << 186%8);*/	/* 0xba left swip */

	if (gsx_gesture->kobj_initialized)
		goto exit_gesture_init;

	ret = kobject_init_and_add(&module->kobj, goodix_get_default_ktype(),
			&core_data->pdev->dev.kobj, "gesture");

	if (ret) {
		ts_err("Create gesture sysfs node error!");
		goto exit_gesture_init;
	}

	for (i = 0; i < ARRAY_SIZE(gesture_attrs); i++) {
		if (sysfs_create_file(&module->kobj,
				&gesture_attrs[i].attr)) {
			ts_err("Create sysfs attr file error");
			kobject_put(&module->kobj);
			goto exit_gesture_init;
		}
	}
	gsx_gesture->kobj_initialized = 1;

exit_gesture_init:
	return 0;
}
static int gsx_gesture_exit(struct goodix_ts_core *core_data,
		struct goodix_ext_module *module)
{
	/*if (gsx_gesture->kobj_initialized)
		kobject_put(&module->kobj);
	gsx_gesture->kobj_initialized = 0;*/
	atomic_set(&gsx_gesture->registered, 0);
	return 0;
}

/**
 * gsx_gesture_ist - Gesture Irq handle
 * This functions is excuted when interrupt happended and
 * ic in doze mode.
 *
 * @core_data: pointer to touch core data
 * @module: pointer to goodix_ext_module struct
 * return: 0 goon execute, EVT_CANCEL_IRQEVT  stop execute
 */
static int gsx_gesture_ist(struct goodix_ts_core *core_data,
	struct goodix_ext_module *module)
{
	int ret;
	unsigned char clear_reg = 0;
	unsigned char checksum = 0, temp_data[GSX_KEY_DATA_LEN];
	struct goodix_ts_device *ts_dev = core_data->ts_dev;
#ifdef CONFIG_GOODIX_HWINFO
	char ch[64] = { 0x0, };
#endif


	/*ts_debug("gsx_gesture_ist, core_data-suspend=%d",
			atomic_read(&core_data->suspended));*/
	/*ts_err("enter %s core_data->suspended=%d\n", __func__, atomic_read(&core_data->suspended));*/

	mutex_lock(&ts_dev->report_mutex);
		/* get ic gesture state*/
	ret = ts_dev->hw_ops->read_trans(core_data->ts_dev, GSX_REG_GESTURE_DATA,
				temp_data, sizeof(temp_data));
	/*ts_err("enter %s temp_data[0]=0x%x ret=%d\n", __func__, temp_data[0], ret);*/
	if (ret < 0 || ((temp_data[0] & GOODIX_GESTURE_EVENT)  == 0)) {
		ts_err("Read gesture data faild, ret=%d, temp_data[0]=0x%x", ret, temp_data[0]);
		goto re_send_ges_cmd;
	}

	if ((temp_data[0] & 0x08)  != 0) {
		FP_Event_Gesture = 1;
		/*ts_debug("FP_Event_Gesture = %d", FP_Event_Gesture);*/
	}

	checksum = checksum_u8(temp_data, sizeof(temp_data));
	if (checksum != 0) {
		ts_err("Gesture data checksum error:0x%x", checksum);
		ts_info("Gesture data %*ph", (int)sizeof(temp_data), temp_data);
		goto re_send_ges_cmd;
	}

	/*ts_debug("temp_data： %*ph", (int)sizeof(temp_data), temp_data);
	ts_debug("FP_Event_Gesture： %d", FP_Event_Gesture);
	ts_debug("udfps_enabled= %d aod_status=%d", core_data->udfps_enabled, core_data->aod_status);
	ts_debug("sleep_finger： %d", !core_data->sleep_finger);*/

	if ((core_data->udfps_enabled || core_data->single_tap_enabled || core_data->aod_status) && FP_Event_Gesture) {
		switch (temp_data[2]) {
		case 0x46:
			core_data->udfps_pressed = 1;
			sysfs_notify(&core_data->pdev->dev.kobj, NULL, "udfps_pressed");
			input_report_key(core_data->input_dev, BTN_INFO, 1);

			core_data->fod_pressed = true;

			ts_debug("Gesture report, x=%d, y=%d, overlapping_area=%d, area=%d",
					x, y, overlapping_area, area);

			/*wait for report key event*/
			FP_Event_Gesture = 0;
			input_sync(core_data->input_dev);
			break;

		case 0x4c:
			/*wait for report key event*/
			FP_Event_Gesture = 0;
			core_data->single_tap_pressed = 1;
			sysfs_notify(&core_data->pdev->dev.kobj, NULL, "single_tap_pressed");
			input_report_key(core_data->input_dev, KEY_GOTO, 1);
			input_sync(core_data->input_dev);
			input_report_key(core_data->input_dev, KEY_GOTO, 0);
			input_sync(core_data->input_dev);
			core_data->sleep_finger = 0;
			ts_debug("Gesture report L");
			break;

		case 0xff:
			if (core_data->fod_pressed) {
				ts_debug("Gesture report up");
				input_report_key(core_data->input_dev, BTN_INFO, 0);
				input_sync(core_data->input_dev);
				core_data->fod_pressed = false;
			}
			core_data->sleep_finger = 0;
			break;
		}
	}

	if (temp_data[2] == 0xcc) {
		/*ts_info("Gesture match success, resume IC");*/
		if (!core_data->double_tap_enabled) {
			goto re_send_ges_cmd;
		}
		core_data->double_tap_pressed = 1;
		sysfs_notify(&core_data->pdev->dev.kobj, NULL, "double_tap_pressed");
#ifdef CONFIG_GOODIX_HWINFO
		if (core_data) {
		core_data->dbclick_count++;
		snprintf(ch, sizeof(ch), "%d", core_data->dbclick_count);
		}
#endif
		goto gesture_ist_exit;

	}
	if (QUERYBIT(gsx_gesture->gesture_type, temp_data[2])) {
		/* do resume routine */
		/*ts_info("Gesture match success, resume IC");*/

		goto gesture_ist_exit;
	} else {
		ts_info("Unsupported gesture:%x", temp_data[2]);
	}

re_send_ges_cmd:
	/*if (ts_dev->hw_ops->send_cmd(core_data->ts_dev, gesture_cmd))
		ts_info("warning: failed re_send gesture cmd\n");*/
	if (goodix_set_suspend_func(core_data) < 0)
		ts_err("set suspend function failed!!");
gesture_ist_exit:
	ts_dev->hw_ops->write_trans(core_data->ts_dev, GSX_REG_GESTURE_DATA,
				&clear_reg, 1);
	mutex_unlock(&ts_dev->report_mutex);
	return EVT_CANCEL_IRQEVT;
}

/**
 *goodix_set_suspend_func - send cmd choose func when at suspend stat
 *When cmd send successful,IC in doze mode
 *
 * @core_data: pointer to touch core data
 * return: 0 send cmd successful, other send cmd failed.
 */
static int goodix_set_suspend_func(struct goodix_ts_core *core_data)
{
	struct goodix_ts_device *dev = core_data->ts_dev;
	u8 state_data[3] = {0};
	int ret;

	int mode = 0;

	if (core_data->double_tap_enabled)
		mode |= 0x01;
	if (core_data->udfps_enabled || core_data->single_tap_enabled)
		mode |= 0x02;

	switch (mode) {
	case 0x03:	// Fod on Dt2w on
		state_data[1] = 0x01;
		state_data[2] = 0xF7;
	break;
	case 0x01:	// Fod off Dt2w on
		state_data[1] = 0x03;
		state_data[2] = 0xF5;
	break;
	case 0x02:	// Fod on Dt2w off
		state_data[1] = 0x00;
		state_data[2] = 0xF8;
	break;
	case 0x00:	// Fod off Dt2w off
		state_data[1] = 0x02;
		state_data[2] = 0xF6;
	break;
	default:
		ret = -1;
		ts_info("Get IC mode failed, core_data->double_tap_enabled=%d, core_data->udfps_enabled=%d;",
			core_data->double_tap_enabled, core_data->udfps_enabled);
	return ret;
	}

	state_data[0] = GSX_GESTURE_CMD;
	ret = goodix_i2c_write(dev, GSX_REG_GESTURE, state_data, 3);
	ts_info("Set IC double wakeup mode %s, FOD mode %s;",
		core_data->double_tap_enabled ? "on" : "off",
		core_data->udfps_enabled ? "on" : "off");

	return ret;
}

/**
 *goodix_wakeup_and_set_suspend_func --- wake up ic from sleep mode and set suspend func
 *invoke this func when ic in sleep mode
 *
 * @core_data: pointer to touch core data
 * return: 0 reset ic stat successful, other failed.
 */
static int goodix_wakeup_and_set_suspend_func(struct goodix_ts_core *core_data)
{
	int r = 0;
	int retry = 0;
	struct goodix_ext_module *ext_module;
	struct goodix_ts_device *ts_dev = core_data->ts_dev;

	/*start resume*/
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry(ext_module, &goodix_modules.head, list) {
			if (!ext_module->funcs->before_resume)
				continue;

			do {
				r = ext_module->funcs->before_resume(core_data, ext_module);
				if (r == EVT_CANCEL_RESUME) {
					ts_info("wait for fwupdate findish");
					mdelay(5);
				}
			} while (r == EVT_CANCEL_RESUME && ++retry < 3);
		}
	}

	if (ts_dev && ts_dev->hw_ops->resume)
		ts_dev->hw_ops->resume(ts_dev);
	goodix_ts_irq_enable(core_data, true);
	/*finish resume*/

	/*start suspend*/
	do {
		r = goodix_set_suspend_func(core_data);
		if (r < 0) {
			ts_info("Send doze command failed, retry");
		}
	} while (r < 0 && ++retry < 3);

	if (core_data->double_tap_enabled && (core_data->udfps_enabled || core_data->single_tap_enabled)) {
		atomic_set(&core_data->suspend_stat, TP_GESTURE_DBCLK_FOD);
	} else if (core_data->double_tap_enabled) {
		atomic_set(&core_data->suspend_stat, TP_GESTURE_DBCLK);
	} else if (core_data->udfps_enabled || core_data->single_tap_enabled) {
		atomic_set(&core_data->suspend_stat, TP_GESTURE_FOD);
	} else {
		atomic_set(&core_data->suspend_stat, TP_SLEEP);
	}
	ts_info("suspend_stat[%d]", atomic_read(&core_data->suspend_stat));

	/*finish suspend*/

	return r;
}


/**
 * gsx_gesture_before_suspend - execute gesture suspend routine
 * This functions is excuted to set ic into doze mode
 *
 * @core_data: pointer to touch core data
 * @module: pointer to goodix_ext_module struct
 * return: 0 goon execute, EVT_IRQCANCLED  stop execute
 */
static int gsx_gesture_before_suspend(struct goodix_ts_core *core_data,
	struct goodix_ext_module *module)
{
	int ret;
	const struct goodix_ts_hw_ops *hw_ops = core_data->ts_dev->hw_ops;
	struct goodix_ts_cmd *gesture_cmd = &gsx_gesture->cmd;

	if (!gesture_cmd->initialized || hw_ops == NULL) {
		ts_err("Uninitialized doze command or hw_ops");
		return 0;
	}

	if (!core_data->gesture_enabled) {
		ts_err("enter %s\n", __func__);
		atomic_set(&core_data->suspended, 1);
		return EVT_CONTINUE;
	}

	ret = goodix_set_suspend_func(core_data);
	if (ret != 0) {
		ts_err("Send doze command error");
		return 0;
	} else {
		ts_info("Set IC in doze mode");
		atomic_set(&core_data->suspended, 1);
		return EVT_CANCEL_SUSPEND;
	}
}

/**
 * gsx_gesture_before_resume - execute gesture resume routine
 * This functions is excuted to make ic out doze mode
 *
 * @core_data: pointer to touch core data
 * @module: pointer to goodix_ext_module struct
 * return: 0 goon execute, EVT_CANCLED  stop execute
 *
static int gsx_gesture_before_resume(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module)
{
	struct goodix_ts_device *ts_dev = core_data->ts_dev;

	ts_dev->hw_ops->reset(ts_dev);
	return 0;
}
*/

static struct goodix_ext_module_funcs gsx_gesture_funcs = {
	.irq_event = gsx_gesture_ist,
	.init = gsx_gesture_init,
	.exit = gsx_gesture_exit,
	.before_suspend = gsx_gesture_before_suspend
};

static int __init goodix_gsx_gesture_init(void)
{
	/* initialize core_data->ts_dev->gesture_cmd*/
	int result;

	ts_info("gesture module init");
	gsx_gesture = kzalloc(sizeof(struct gesture_module), GFP_KERNEL);
	if (!gsx_gesture)
		result = -ENOMEM;
	gsx_gesture->module.funcs = &gsx_gesture_funcs;
	gsx_gesture->module.priority = EXTMOD_PRIO_GESTURE;
	gsx_gesture->module.name = "Goodix_gsx_gesture";
	gsx_gesture->module.priv_data = gsx_gesture;
	gsx_gesture->kobj_initialized = 0;
	atomic_set(&gsx_gesture->registered, 0);
	rwlock_init(&gsx_gesture->rwlock);

	result = goodix_register_ext_module(&(gsx_gesture->module));
	if (result == 0)
		atomic_set(&gsx_gesture->registered, 1);

	return result;
}

static void __exit goodix_gsx_gesture_exit(void)
{
	ts_info("gesture module exit");
	if (gsx_gesture->kobj_initialized)
		kobject_put(&gsx_gesture->module.kobj);
	kfree(gsx_gesture);
	return;
}

module_init(goodix_gsx_gesture_init);
module_exit(goodix_gsx_gesture_exit);

MODULE_DESCRIPTION("Goodix gsx Touchscreen Gesture Module");
MODULE_AUTHOR("Goodix, Inc.");
MODULE_LICENSE("GPL v2");
