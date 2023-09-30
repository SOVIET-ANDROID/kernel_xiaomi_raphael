#include <linux/export.h>
#include <linux/pm_qos.h>
#include <linux/uaccess.h>

int pm_qos_request(int pm_qos_class)
{
	return 0;
}
EXPORT_SYMBOL_GPL(pm_qos_request);

int pm_qos_request_for_cpu(int pm_qos_class, int cpu)
{
	return 0;
}
EXPORT_SYMBOL(pm_qos_request_for_cpu);

int pm_qos_request_active(struct pm_qos_request *req)
{
	return 0;
}
EXPORT_SYMBOL_GPL(pm_qos_request_active);

int pm_qos_request_for_cpumask(int pm_qos_class, struct cpumask *mask)
{
	return 0;
}
EXPORT_SYMBOL(pm_qos_request_for_cpumask);

void pm_qos_add_request(struct pm_qos_request *req,
			int pm_qos_class, s32 value)
{
}
EXPORT_SYMBOL_GPL(pm_qos_add_request);

void __always_inline pm_qos_update_request(struct pm_qos_request *req,
			   s32 new_value)
{
}
EXPORT_SYMBOL_GPL(pm_qos_update_request);

void pm_qos_remove_request(struct pm_qos_request *req)
{
}
EXPORT_SYMBOL_GPL(pm_qos_remove_request);

int pm_qos_add_notifier(int pm_qos_class, struct notifier_block *notifier)
{
	return 0;
}
EXPORT_SYMBOL_GPL(pm_qos_add_notifier);

int pm_qos_remove_notifier(int pm_qos_class, struct notifier_block *notifier)
{
	return 0;
}
EXPORT_SYMBOL_GPL(pm_qos_remove_notifier);

bool pm_qos_update_flags(struct pm_qos_flags *pqf,
			 struct pm_qos_flags_request *req,
			 enum pm_qos_req_action action, s32 val)
{
	return 1;
}


int __always_inline pm_qos_update_target(struct pm_qos_constraints *c, struct plist_node *node,
			 enum pm_qos_req_action action, int value)
{
	return 1;
}

s32 pm_qos_read_value(struct pm_qos_constraints *c)
{
	return 100;
}
