/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/pm_wakeup.h>

static char system_stat[8];
static struct kobject *state_kobj;
static struct wakeup_source *notify_resume_ws;
static atomic_t counter = ATOMIC_INIT(0);

static int suspend_resume_notifier(struct notifier_block *nb,
				unsigned long event, void *unused)
{
	switch (event) {

	case PM_POST_SUSPEND:
		__pm_stay_awake(notify_resume_ws);
		atomic_inc(&counter);
		strlcpy(system_stat, "resume", sizeof(system_stat));
		sysfs_notify(state_kobj, NULL, "system_stat");
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block sdx_power_pm_nb = {
	.notifier_call = suspend_resume_notifier,
	.priority = INT_MAX,
};


static ssize_t state_attr_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return snprintf(buf, sizeof(system_stat), "%s\n", system_stat);
}

static ssize_t state_attr_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	char *p;
	int len;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	if (len == 6 && strcmp(buf, "online")) {
		strlcpy(system_stat, "online", sizeof(system_stat));

		if (atomic_cmpxchg(&counter, 1, 0))
			__pm_relax(notify_resume_ws);
		return n;
	} else {
		return -EINVAL;
	}
}

static struct kobj_attribute system_state_attribute =
	__ATTR(system_stat, 0664, state_attr_show, state_attr_store);

static struct attribute *attrs[] = {
	&system_state_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static int __init sdx_power_init(void)
{

	int error;
	/* Create a debug node where system state is written */
	state_kobj = kobject_create_and_add("pwr_state", NULL);
	if (!state_kobj)
		return -ENOMEM;

	error = sysfs_create_group(state_kobj, &attr_group);
	if (error) {
		pr_err("%s: sysfs create group failed %d\n", __func__, error);
		goto err_create_group;
	}

	strlcpy(system_stat, "resume", sizeof(system_stat));
	error = register_pm_notifier(&sdx_power_pm_nb);
	if (error) {
		pr_err("%s: register_pm_notifier error %d\n", __func__, error);
		goto free_sysfs;
	}
/* Register as a wakeup source */
	notify_resume_ws = wakeup_source_register(NULL, "notify_resume");
	if (!notify_resume_ws) {
		error = -ENOMEM;
		goto err_wakeup_source_register;
	}
	__pm_stay_awake(notify_resume_ws);
	atomic_inc(&counter);
	return 0;

err_wakeup_source_register:
	unregister_pm_notifier(&sdx_power_pm_nb);

free_sysfs:
	sysfs_remove_group(state_kobj, &attr_group);

err_create_group:
	kobject_put(state_kobj);
	state_kobj = NULL;
	return error;
}
subsys_initcall(sdx_power_init);

MODULE_LICENSE("GPL v2");
