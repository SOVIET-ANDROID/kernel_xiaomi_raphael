// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#define pr_fmt(fmt) "msm_thermal_simple: " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define OF_READ_U32(node, prop, dst)						\
({										\
	int ret = of_property_read_u32(node, prop, &(dst));			\
	if (ret)								\
		pr_err("%s: " prop " property missing\n", (node)->name);	\
	ret;									\
})

struct thermal_zone {
	u32 gold_khz;
	u32 prime_khz;
	u32 silver_khz;
	s32 trip_deg;
};

struct thermal_drv {
	struct notifier_block cpu_notif;
	struct delayed_work throttle_work;
	struct workqueue_struct *wq;
	struct thermal_zone *zones;
	struct thermal_zone *curr_zone;
	u32 poll_jiffies;
	u32 start_delay;
	u32 nr_zones;
};

static void update_online_cpu_policy(void)
{
	unsigned int cpu;

	get_online_cpus();
	for_each_possible_cpu(cpu) {
		if (cpu_online(cpu)) {
			if (cpumask_intersects(cpumask_of(cpu), cpu_lp_mask))
				cpufreq_update_policy(cpu);
			if (cpumask_intersects(cpumask_of(cpu), cpu_perf_mask))
				cpufreq_update_policy(cpu);
			if (cpumask_intersects(cpumask_of(cpu), cpu_prime_mask))
				cpufreq_update_policy(cpu);
		}
	}
	put_online_cpus();
}

static void thermal_throttle_worker(struct work_struct *work)
{
	struct thermal_drv *t = container_of(to_delayed_work(work), typeof(*t),
					     throttle_work);
	struct thermal_zone *new_zone, *old_zone;
	int temp = 0, temp_gpu = 0;
	s64 temp_total = 0, temp_avg = 0;
	short i = 0;

	/* Store average temperature of all CPU cores */
	for (i; i < NR_CPUS; i++) {
		char zone_name[15];
		sprintf(zone_name, "cpu-1-%i-usr", i);
		thermal_zone_get_temp(thermal_zone_get_zone_by_name(zone_name), &temp);
		temp_total += temp;
	}

	temp_avg = temp_total / NR_CPUS;

	/* Checking GPU temperature */
	thermal_zone_get_temp(thermal_zone_get_zone_by_name("gpuss-0-usr"), &temp_gpu);

	/* (Number of CPUs * 8) + current temp of the GPU,
	   this will add an overlay on top of the current cpu
	   temperature and make the thermal_simple driver set 
	   a zone above the one it should, decreasing temps in
	   games or GPU heavy tasks while maintaining good CPU
	   performance in CPU only tasks */

	if (temp_gpu >= 63000)
		/* GPU started to get hot, using base values
		   so throttling is not so agressive at this point. */
		temp_avg = (temp_total + 35000) / NR_CPUS;
	else if (temp_gpu >= 65000)
		temp_avg = (temp_total + 55000) / NR_CPUS;
	else if (temp_gpu >= 68000)
		temp_avg = (temp_total + 65000) / NR_CPUS;
	else if (temp_gpu >= 70000)
		temp_avg = (temp_total + temp_gpu) / NR_CPUS;
		
	old_zone = t->curr_zone;
	new_zone = NULL;

	for (i = t->nr_zones - 1; i >= 0; i--) {
		if (temp_avg >= t->zones[i].trip_deg) {
			new_zone = t->zones + i;
			break;
		}
	}

	/* Update thermal zone if it changed */
	if (new_zone != old_zone) {
		pr_info("temp_avg: %i, temp_gpu: %i\n", temp_avg, temp_gpu);
		t->curr_zone = new_zone;
		update_online_cpu_policy();
	}

	queue_delayed_work(t->wq, &t->throttle_work, t->poll_jiffies);
}

static u32 get_throttle_freq(struct thermal_zone *zone, u32 cpu)
{
	if (cpumask_test_cpu(cpu, cpu_lp_mask))
		return zone->silver_khz;
	else if (cpumask_test_cpu(cpu, cpu_perf_mask))
		return zone->gold_khz;

	return zone->prime_khz;
}

static int cpu_notifier_cb(struct notifier_block *nb, unsigned long val,
			   void *data)
{
	struct thermal_drv *t = container_of(nb, typeof(*t), cpu_notif);
	struct cpufreq_policy *policy = data;
	struct thermal_zone *zone;

	if (val != CPUFREQ_ADJUST)
		return NOTIFY_OK;

	zone = t->curr_zone;
	if (zone)
		policy->max = get_throttle_freq(zone, policy->cpu);
	else
		policy->max = policy->user_policy.max;

	if (policy->max < policy->min)
		policy->min = policy->max;

	return NOTIFY_OK;
}

static int msm_thermal_simple_parse_dt(struct platform_device *pdev,
				       struct thermal_drv *t)
{
	struct device_node *child, *node = pdev->dev.of_node;
	int ret;

	ret = OF_READ_U32(node, "qcom,poll-ms", t->poll_jiffies);
	if (ret)
		return ret;

	/* Specifying a start delay is optional */
	OF_READ_U32(node, "qcom,start-delay", t->start_delay);

	/* Convert polling milliseconds to jiffies */
	t->poll_jiffies = msecs_to_jiffies(t->poll_jiffies);

	/* Calculate the number of zones */
	for_each_child_of_node(node, child)
		t->nr_zones++;

	if (!t->nr_zones) {
		pr_err("No zones specified\n");
		return -EINVAL;
	}

	t->zones = kmalloc(t->nr_zones * sizeof(*t->zones), GFP_KERNEL);
	if (!t->zones)
		return -ENOMEM;

	for_each_child_of_node(node, child) {
		struct thermal_zone *zone;
		u32 reg;

		ret = OF_READ_U32(child, "reg", reg);
		if (ret)
			goto free_zones;

		zone = t->zones + reg;

		ret = OF_READ_U32(child, "qcom,silver-khz", zone->silver_khz);
		if (ret)
			goto free_zones;

		ret = OF_READ_U32(child, "qcom,gold-khz", zone->gold_khz);
		if (ret)
			goto free_zones;

		ret = OF_READ_U32(child, "qcom,prime-khz", zone->prime_khz);
		if (ret)
			goto free_zones;

		ret = OF_READ_U32(child, "qcom,trip-deg", zone->trip_deg);
		if (ret)
			goto free_zones;
	}

	return 0;

free_zones:
	kfree(t->zones);
	return ret;
}

static int msm_thermal_simple_probe(struct platform_device *pdev)
{
	struct thermal_drv *t;
	int ret;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	t->wq = alloc_workqueue("msm_thermal_simple",
				WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (!t->wq) {
		ret = -ENOMEM;
		goto free_t;
	}

	ret = msm_thermal_simple_parse_dt(pdev, t);
	if (ret)
		goto destroy_wq;

	/* Set the priority to INT_MIN so throttling can't be tampered with */
	t->cpu_notif.notifier_call = cpu_notifier_cb;
	t->cpu_notif.priority = INT_MIN;
	ret = cpufreq_register_notifier(&t->cpu_notif, CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		pr_err("Failed to register cpufreq notifier, err: %d\n", ret);
		goto free_zones;
	}

	/* Fire up the persistent worker */
	INIT_DELAYED_WORK(&t->throttle_work, thermal_throttle_worker);
	queue_delayed_work(t->wq, &t->throttle_work, t->start_delay * HZ);

	return 0;

free_zones:
	kfree(t->zones);
destroy_wq:
	destroy_workqueue(t->wq);
free_t:
	kfree(t);
	return ret;
}

static const struct of_device_id msm_thermal_simple_match_table[] = {
	{ .compatible = "qcom,msm-thermal-simple" },
	{ }
};

static struct platform_driver msm_thermal_simple_device = {
	.probe = msm_thermal_simple_probe,
	.driver = {
		.name = "msm-thermal-simple",
		.owner = THIS_MODULE,
		.of_match_table = msm_thermal_simple_match_table
	}
};

static int __init msm_thermal_simple_init(void)
{
	return platform_driver_register(&msm_thermal_simple_device);
}
device_initcall(msm_thermal_simple_init);
