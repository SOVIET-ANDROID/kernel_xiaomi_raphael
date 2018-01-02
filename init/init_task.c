// SPDX-License-Identifier: GPL-2.0
#include <linux/init_task.h>
#include <linux/export.h>
#include <linux/mqueue.h>
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/scs.h>

#include <asm/pgtable.h>
#include <linux/uaccess.h>

static struct signal_struct init_signals = {
	.nr_threads	= 1,
	.thread_head	= LIST_HEAD_INIT(init_task.thread_node),
	.wait_chldexit	= __WAIT_QUEUE_HEAD_INITIALIZER(init_signals.wait_chldexit),
	.shared_pending	= {
		.list = LIST_HEAD_INIT(init_signals.shared_pending.list),
		.signal =  {{0}}
	},
	.rlim		= INIT_RLIMITS,
	.cred_guard_mutex = __MUTEX_INITIALIZER(init_signals.cred_guard_mutex),
#ifdef CONFIG_POSIX_TIMERS
	.posix_timers = LIST_HEAD_INIT(init_signals.posix_timers),
	.cputimer	= {
		.cputime_atomic	= INIT_CPUTIME_ATOMIC,
		.running	= false,
		.checking_timer = false,
	},
#endif
	INIT_CPU_TIMERS(init_signals)
	.leader_pid = &init_struct_pid,
	INIT_PREV_CPUTIME(init_signals)
};

static struct sighand_struct init_sighand = {
	.count		= ATOMIC_INIT(1),
	.action		= { { { .sa_handler = SIG_DFL, } }, },
	.siglock	= __SPIN_LOCK_UNLOCKED(init_sighand.siglock),
	.signalfd_wqh	= __WAIT_QUEUE_HEAD_INITIALIZER(init_sighand.signalfd_wqh),
};

/* Initial task structure */
struct task_struct init_task
#ifdef CONFIG_ARCH_TASK_STRUCT_ON_STACK
	__init_task_data
#endif
	= INIT_TASK(init_task);
EXPORT_SYMBOL(init_task);

#ifdef CONFIG_SHADOW_CALL_STACK
unsigned long init_shadow_call_stack[SCS_SIZE / sizeof(long)] __init_task_data
		__aligned(SCS_SIZE) = {
	[(SCS_SIZE / sizeof(long)) - 1] = SCS_END_MAGIC
};
#endif

/*
 * Initial thread structure. Alignment of this is handled by a special
 * linker map entry.
 */
#ifndef CONFIG_THREAD_INFO_IN_TASK
struct thread_info init_thread_info __init_thread_info = INIT_THREAD_INFO(init_task);
#endif
