// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/ftrace.h>
#include <trace/events/sched.h>
#include <linux/sysfs.h>

#define RECORD_CMDLINE 1
#define RECORD_TGID 2

static int sched_flags = 0;

static void probe_sched_switch(void *ignore, bool preempt,
                               struct task_struct *prev, struct task_struct *next)
{
    if (sched_flags & RECORD_CMDLINE)
        tracing_record_taskinfo_sched_switch(prev, next, RECORD_CMDLINE);
}

static void probe_sched_wakeup(void *ignore, struct task_struct *wakee)
{
    if (sched_flags & RECORD_TGID)
        tracing_record_taskinfo(wakee, RECORD_TGID);
}

static int tracing_sched_register(void)
{
    int ret;

    if (sched_flags & RECORD_CMDLINE) {
        ret = register_trace_sched_switch(probe_sched_switch, NULL);
        if (ret)
            return ret;
    }

    if (sched_flags & RECORD_TGID) {
        ret = register_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
        if (ret) {
            if (sched_flags & RECORD_CMDLINE)
                unregister_trace_sched_switch(probe_sched_switch, NULL);
            return ret;
        }
    }

    return 0;
}

static void tracing_sched_unregister(void)
{
    if (sched_flags & RECORD_CMDLINE)
        unregister_trace_sched_switch(probe_sched_switch, NULL);
    if (sched_flags & RECORD_TGID)
        unregister_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
}

void set_sched_flags(int flags)
{
    sched_flags = flags;
    tracing_sched_unregister();
    tracing_sched_register();
}

void cleanup_tracing(void)
{
    tracing_sched_unregister();
}
