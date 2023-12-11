/*
 * Copyright (c) 2014-2019, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt) "arm-memlat-mon: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/cpu_pm.h>
#include <linux/cpu.h>
#include <linux/of_fdt.h>
#include "governor.h"
#include "governor_memlat.h"
#include <linux/perf_event.h>
#include <linux/of_device.h>
#include <soc/qcom/scm.h>

enum ev_index {
	INST_IDX,
	CM_IDX,
	CYC_IDX,
	STALL_CYC_IDX,
	NUM_EVENTS
};
#define INST_EV		0x08
#define L2DM_EV		0x17
#define CYC_EV		0x11

struct event_data {
	struct perf_event *pevent;
	unsigned long prev_count;
};

struct cpu_pmu_stats {
	struct event_data events[NUM_EVENTS];
	ktime_t prev_ts;
};

struct cpu_grp_info {
	cpumask_t cpus;
	unsigned long any_cpu_ev_mask;
	unsigned int event_ids[NUM_EVENTS];
	struct cpu_pmu_stats *cpustats;
	struct memlat_hwmon hw;
};

struct memlat_mon_spec {
	bool is_compute;
};

struct ipi_data {
	unsigned long cnts[NR_CPUS][NUM_EVENTS];
	struct task_struct *waiter_task;
	struct cpu_grp_info *cpu_grp;
	atomic_t cpus_left;
};

#define to_cpustats(cpu_grp, cpu) \
	(&cpu_grp->cpustats[cpu - cpumask_first(&cpu_grp->cpus)])
#define to_devstats(cpu_grp, cpu) \
	(&cpu_grp->hw.core_stats[cpu - cpumask_first(&cpu_grp->cpus)])
#define to_cpu_grp(hwmon) container_of(hwmon, struct cpu_grp_info, hw)


static unsigned long compute_freq(struct cpu_pmu_stats *cpustats,
						unsigned long cyc_cnt)
{
	ktime_t ts;
	unsigned int diff;
	uint64_t freq = 0;

	ts = ktime_get();
	diff = ktime_to_us(ktime_sub(ts, cpustats->prev_ts));
	if (!diff)
		diff = 1;
	cpustats->prev_ts = ts;
	freq = cyc_cnt;
	do_div(freq, diff);

	return freq;
}

#define MAX_COUNT_LIM 0xFFFFFFFFFFFFFFFF
static unsigned long read_event(struct cpu_pmu_stats *cpustats, int event_id)
{
	struct event_data *event = &cpustats->events[event_id];
	unsigned long ev_count;
	u64 total;

	if (!event->pevent || perf_event_read_local(event->pevent, &total))
		return 0;

	ev_count = total - event->prev_count;
	event->prev_count = total;
	return ev_count;
}

static void read_perf_counters(struct ipi_data *ipd, int cpu)
{
	struct cpu_grp_info *cpu_grp = ipd->cpu_grp;
	struct cpu_pmu_stats *cpustats = to_cpustats(cpu_grp, cpu);
	int ev;

	for (ev = 0; ev < NUM_EVENTS; ev++) {
		if (!(cpu_grp->any_cpu_ev_mask & BIT(ev)))
			ipd->cnts[cpu][ev] = read_event(cpustats, ev);
	}
}

static void read_evs_ipi(void *info)
{
	int cpu = raw_smp_processor_id();
	struct ipi_data *ipd = info;
	struct task_struct *waiter;

	read_perf_counters(ipd, cpu);

	/*
	 * Wake up the waiter task if we're the final CPU. The ipi_data pointer
	 * isn't safe to dereference once cpus_left reaches zero, so the waiter
	 * task_struct pointer must be cached before that. Also defend against
	 * the extremely unlikely possibility that the waiter task will have
	 * exited by the time wake_up_process() is reached.
	 */
	waiter = ipd->waiter_task;
	get_task_struct(waiter);
	if (atomic_fetch_andnot(BIT(cpu), &ipd->cpus_left) == BIT(cpu) &&
	    waiter->state != TASK_RUNNING)
		wake_up_process(waiter);
	put_task_struct(waiter);
}

static void read_any_cpu_events(struct ipi_data *ipd, unsigned long cpus)
{
	struct cpu_grp_info *cpu_grp = ipd->cpu_grp;
	int cpu, ev;

	if (!cpu_grp->any_cpu_ev_mask)
		return;

	for_each_cpu(cpu, to_cpumask(&cpus)) {
		struct cpu_pmu_stats *cpustats = to_cpustats(cpu_grp, cpu);

		for_each_set_bit(ev, &cpu_grp->any_cpu_ev_mask, NUM_EVENTS)
			ipd->cnts[cpu][ev] = read_event(cpustats, ev);
	}
}

static void compute_perf_counters(struct ipi_data *ipd, int cpu)
{
	struct cpu_grp_info *cpu_grp = ipd->cpu_grp;
	struct cpu_pmu_stats *cpustats = to_cpustats(cpu_grp, cpu);
	struct dev_stats *devstats = to_devstats(cpu_grp, cpu);
	unsigned long cyc_cnt, stall_cnt;

	devstats->inst_count = ipd->cnts[cpu][INST_IDX];
	devstats->mem_count = ipd->cnts[cpu][CM_IDX];
	cyc_cnt = ipd->cnts[cpu][CYC_IDX];
	devstats->freq = compute_freq(cpustats, cyc_cnt);
	if (cpustats->events[STALL_CYC_IDX].pevent) {
		stall_cnt = ipd->cnts[cpu][STALL_CYC_IDX];
		stall_cnt = min(stall_cnt, cyc_cnt);
		devstats->stall_pct = mult_frac(100, stall_cnt, cyc_cnt);
	} else {
		devstats->stall_pct = 100;
	}
}

static unsigned long get_cnt(struct memlat_hwmon *hw)
{
	struct cpu_grp_info *cpu_grp = to_cpu_grp(hw);
	unsigned long cpus_read_mask, tmp_mask;
	call_single_data_t csd[NR_CPUS];
	struct ipi_data ipd;
	int cpu, this_cpu;

	ipd.waiter_task = current;
	ipd.cpu_grp = cpu_grp;

	/* Dispatch asynchronous IPIs to each CPU to read the perf events */
	cpus_read_lock();
	preempt_disable();
	this_cpu = raw_smp_processor_id();
	cpus_read_mask = *cpumask_bits(&cpu_grp->cpus);
	tmp_mask = cpus_read_mask & ~BIT(this_cpu);
	ipd.cpus_left = (atomic_t)ATOMIC_INIT(tmp_mask);
	for_each_cpu(cpu, to_cpumask(&tmp_mask)) {
		/*
		 * Some SCM calls take very long (20+ ms), so the IPI could lag
		 * on the CPU running the SCM call. Skip offline CPUs too.
		 */
		csd[cpu].flags = 0;
		if (under_scm_call(cpu) ||
		    generic_exec_single(cpu, &csd[cpu], read_evs_ipi, &ipd))
			cpus_read_mask &= ~BIT(cpu);
	}
	cpus_read_unlock();
	/* Read this CPU's events while the IPIs run */
	if (cpus_read_mask & BIT(this_cpu))
		read_perf_counters(&ipd, this_cpu);
	preempt_enable();

	/* Bail out if there weren't any CPUs available */
	if (!cpus_read_mask)
		return 0;

	/* Read any any-CPU events while the IPIs run */
	read_any_cpu_events(&ipd, cpus_read_mask);

	/* Clear out CPUs which were skipped */
	atomic_andnot(cpus_read_mask ^ tmp_mask, &ipd.cpus_left);

	/*
	 * Wait until all the IPIs are done reading their events, and compute
	 * each finished CPU's results while waiting since some CPUs may finish
	 * reading their events faster than others.
	 */
	for (tmp_mask = cpus_read_mask;;) {
		unsigned long cpus_done, cpus_left;

		set_current_state(TASK_UNINTERRUPTIBLE);
		cpus_left = (unsigned int)atomic_read(&ipd.cpus_left);
		if ((cpus_done = cpus_left ^ tmp_mask)) {
			for_each_cpu(cpu, to_cpumask(&cpus_done))
				compute_perf_counters(&ipd, cpu);
			if (!cpus_left)
				break;
			tmp_mask = cpus_left;
		} else {
			schedule();
		}
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}

static void delete_events(struct cpu_pmu_stats *cpustats)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cpustats->events); i++) {
		cpustats->events[i].prev_count = 0;
		if (cpustats->events[i].pevent) {
			perf_event_release_kernel(cpustats->events[i].pevent);
			cpustats->events[i].pevent = NULL;
		}
	}
}

static void stop_hwmon(struct memlat_hwmon *hw)
{
	int cpu;
	struct cpu_grp_info *cpu_grp = to_cpu_grp(hw);
	struct dev_stats *devstats;

	for_each_cpu(cpu, &cpu_grp->cpus) {
		delete_events(to_cpustats(cpu_grp, cpu));

		/* Clear governor data */
		devstats = to_devstats(cpu_grp, cpu);
		devstats->inst_count = 0;
		devstats->mem_count = 0;
		devstats->freq = 0;
		devstats->stall_pct = 0;
	}
}

static struct perf_event_attr *alloc_attr(void)
{
	struct perf_event_attr *attr;

	attr = kzalloc(sizeof(struct perf_event_attr), GFP_KERNEL);
	if (!attr)
		return attr;

	attr->type = PERF_TYPE_RAW;
	attr->size = sizeof(struct perf_event_attr);
	attr->pinned = 1;
	attr->exclude_idle = 1;

	return attr;
}

static int set_events(struct cpu_grp_info *cpu_grp, int cpu)
{
	struct perf_event *pevent;
	struct perf_event_attr *attr;
	int err, i;
	unsigned int event_id;
	struct cpu_pmu_stats *cpustats = to_cpustats(cpu_grp, cpu);

	/* Allocate an attribute for event initialization */
	attr = alloc_attr();
	if (!attr)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(cpustats->events); i++) {
		event_id = cpu_grp->event_ids[i];
		if (!event_id)
			continue;

		attr->config = event_id;
		pevent = perf_event_create_kernel_counter(attr, cpu, NULL,
							  NULL, NULL);
		if (IS_ERR(pevent))
			goto err_out;
		cpustats->events[i].pevent = pevent;
		perf_event_enable(pevent);
		if (cpumask_equal(&pevent->readable_on_cpus, &CPU_MASK_ALL))
			cpu_grp->any_cpu_ev_mask |= BIT(i);
	}

	kfree(attr);
	return 0;

err_out:
	err = PTR_ERR(pevent);
	kfree(attr);
	return err;
}

static int start_hwmon(struct memlat_hwmon *hw)
{
	int cpu, ret = 0;
	struct cpu_grp_info *cpu_grp = to_cpu_grp(hw);

	for_each_cpu(cpu, &cpu_grp->cpus) {
		ret = set_events(cpu_grp, cpu);
		if (ret) {
			pr_warn("Perf event init failed on CPU%d\n", cpu);
			break;
		}
	}

	return ret;
}

static int get_mask_from_dev_handle(struct platform_device *pdev,
					cpumask_t *mask)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_phandle;
	struct device *cpu_dev;
	int cpu, i = 0;
	int ret = -ENOENT;

	dev_phandle = of_parse_phandle(dev->of_node, "qcom,cpulist", i++);
	while (dev_phandle) {
		for_each_possible_cpu(cpu) {
			cpu_dev = get_cpu_device(cpu);
			if (cpu_dev && cpu_dev->of_node == dev_phandle) {
				cpumask_set_cpu(cpu, mask);
				ret = 0;
				break;
			}
		}
		dev_phandle = of_parse_phandle(dev->of_node,
						"qcom,cpulist", i++);
	}

	return ret;
}

static struct device_node *parse_child_nodes(struct device *dev)
{
	struct device_node *of_child;
	int ddr_type_of = -1;
	int ddr_type = of_fdt_get_ddrtype();
	int ret;

	for_each_child_of_node(dev->of_node, of_child) {
		ret = of_property_read_u32(of_child, "qcom,ddr-type",
							&ddr_type_of);
		if (!ret && (ddr_type == ddr_type_of)) {
			dev_dbg(dev,
				"ddr-type = %d, is matching DT entry\n",
				ddr_type_of);
			return of_child;
		}
	}
	return NULL;
}

static int arm_memlat_mon_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct memlat_hwmon *hw;
	struct cpu_grp_info *cpu_grp;
	const struct memlat_mon_spec *spec;
	int cpu, ret;
	u32 event_id;

	cpu_grp = devm_kzalloc(dev, sizeof(*cpu_grp), GFP_KERNEL);
	if (!cpu_grp)
		return -ENOMEM;
	hw = &cpu_grp->hw;

	hw->dev = dev;
	hw->of_node = of_parse_phandle(dev->of_node, "qcom,target-dev", 0);
	if (!hw->of_node) {
		dev_err(dev, "Couldn't find a target device\n");
		return -ENODEV;
	}

	if (get_mask_from_dev_handle(pdev, &cpu_grp->cpus)) {
		dev_err(dev, "CPU list is empty\n");
		return -ENODEV;
	}

	hw->num_cores = cpumask_weight(&cpu_grp->cpus);
	hw->core_stats = devm_kcalloc(dev,
				      hw->num_cores,
				      sizeof(*(hw->core_stats)),
				      GFP_KERNEL);
	if (!hw->core_stats)
		return -ENOMEM;

	cpu_grp->cpustats = devm_kcalloc(dev,
					 hw->num_cores,
					 sizeof(*(cpu_grp->cpustats)),
					 GFP_KERNEL);
	if (!cpu_grp->cpustats)
		return -ENOMEM;

	cpu_grp->event_ids[CYC_IDX] = CYC_EV;

	for_each_cpu(cpu, &cpu_grp->cpus)
		to_devstats(cpu_grp, cpu)->id = cpu;

	hw->start_hwmon = &start_hwmon;
	hw->stop_hwmon = &stop_hwmon;
	hw->get_cnt = &get_cnt;
	if (of_get_child_count(dev->of_node))
		hw->get_child_of_node = &parse_child_nodes;

	spec = of_device_get_match_data(dev);
	if (spec && spec->is_compute) {
		ret = register_compute(dev, hw);
		if (ret)
			pr_err("Compute Gov registration failed\n");

		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "qcom,cachemiss-ev",
				   &event_id);
	if (ret) {
		dev_dbg(dev, "Cache Miss event not specified. Using def:0x%x\n",
			L2DM_EV);
		event_id = L2DM_EV;
	}
	cpu_grp->event_ids[CM_IDX] = event_id;

	ret = of_property_read_u32(dev->of_node, "qcom,inst-ev", &event_id);
	if (ret) {
		dev_dbg(dev, "Inst event not specified. Using def:0x%x\n",
			INST_EV);
		event_id = INST_EV;
	}
	cpu_grp->event_ids[INST_IDX] = event_id;

	ret = of_property_read_u32(dev->of_node, "qcom,stall-cycle-ev",
				   &event_id);
	if (ret)
		dev_dbg(dev, "Stall cycle event not specified. Event ignored.\n");
	else
		cpu_grp->event_ids[STALL_CYC_IDX] = event_id;

	ret = register_memlat(dev, hw);
	if (ret)
		pr_err("Mem Latency Gov registration failed\n");

	return ret;
}

static const struct memlat_mon_spec spec[] = {
	[0] = { false },
	[1] = { true },
};

static const struct of_device_id memlat_match_table[] = {
	{ .compatible = "qcom,arm-memlat-mon", .data = &spec[0] },
	{ .compatible = "qcom,arm-cpu-mon", .data = &spec[1] },
	{}
};

static struct platform_driver arm_memlat_mon_driver = {
	.probe = arm_memlat_mon_driver_probe,
	.driver = {
		.name = "arm-memlat-mon",
		.of_match_table = memlat_match_table,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(arm_memlat_mon_driver);
