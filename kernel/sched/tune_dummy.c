#include <linux/cgroup.h>
#include "sched.h"

#define BOOSTGROUPS_COUNT 5

struct schedtune {
	/* SchedTune CGroup subsystem */
	struct cgroup_subsys_state css;

	/* Boost value for tasks on that SchedTune CGroup */
	int boost;

	/* Hint to bias scheduling of tasks on that SchedTune CGroup
	 * towards idle CPUs */
	int prefer_idle;
};

static struct schedtune
root_schedtune = {
	.boost	= 0,
	.prefer_idle = 0,
};

static struct schedtune *allocated_group[BOOSTGROUPS_COUNT] = {
	&root_schedtune,
	NULL,
};

static inline struct schedtune *css_st(struct cgroup_subsys_state *css)
{
	return container_of(css, struct schedtune, css);
}

static u64
prefer_idle_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return 0;
}

static int
prefer_idle_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    u64 prefer_idle)
{
	return 0;
}

static u64
prefer_high_cap_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return 0;
}

static int
prefer_high_cap_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    u64 prefer_idle)
{
	return 0;
}

static s64
boost_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return 0;
}

static int
boost_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    s64 boost)
{
	return 0;
}

static struct cftype files[] = {
	{
		.name = "boost",
		.read_s64 = boost_read,
		.write_s64 = boost_write,
	},
	{
		.name = "prefer_idle",
		.read_u64 = prefer_idle_read,
		.write_u64 = prefer_idle_write,
	},
	{
		.name = "prefer_high_cap",
		.read_u64 = prefer_high_cap_read,
		.write_u64 = prefer_high_cap_write,
	},
	{ }	/* terminate */
};

static struct cgroup_subsys_state *
schedtune_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct schedtune *st;
	int idx;

	if (!parent_css)
		return &root_schedtune.css;

	/* Allow only single level hierachies */
	if (parent_css != &root_schedtune.css) {
		pr_err("Nested SchedTune boosting groups not allowed\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Allow only a limited number of boosting groups */
	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx)
		if (!allocated_group[idx])
			break;
	if (idx == BOOSTGROUPS_COUNT) {
		pr_err("Trying to create more than %d SchedTune boosting groups\n",
		       BOOSTGROUPS_COUNT);
		return ERR_PTR(-ENOSPC);
	}

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto out;

	return &st->css;

out:
	return ERR_PTR(-ENOMEM);
}

static void
schedtune_css_free(struct cgroup_subsys_state *css)
{
	struct schedtune *st = css_st(css);

	kfree(st);
}

struct cgroup_subsys schedtune_cgrp_subsys = {
	.css_alloc	= schedtune_css_alloc,
	.css_free	= schedtune_css_free,
	.legacy_cftypes	= files,
	.early_init	= 1,
};
