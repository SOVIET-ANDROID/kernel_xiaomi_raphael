#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/printk.h>

#include "allowlist.h"
#include "arch.h"
#include "core_hook.h"
#include "klog.h"
#include "ksu.h"
#include "throne_tracker.h"

static struct workqueue_struct *ksu_workqueue;

static const struct file_operations ksu_fops = {
    .owner = THIS_MODULE,
};

static struct miscdevice ksu_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "ksu",
    .fops  = &ksu_fops,
};

static void ksu_device_create(void)
{
    int ret = misc_register(&ksu_misc_device);
    if (ret)
        pr_err("ksu: failed to register misc device\n");
}

bool ksu_queue_work(struct work_struct *work)
{
    return queue_work(ksu_workqueue, work);
}

extern int ksu_handle_execveat_sucompat(int *fd, struct filename **filename_ptr,
                                        void *argv, void *envp, int *flags);
extern int ksu_handle_execveat_ksud(int *fd, struct filename **filename_ptr,
                                    void *argv, void *envp, int *flags);

int ksu_handle_execveat(int *fd, struct filename **filename_ptr, void *argv,
                        void *envp, int *flags)
{
    ksu_handle_execveat_ksud(fd, filename_ptr, argv, envp, flags);
    return ksu_handle_execveat_sucompat(fd, filename_ptr, argv, envp, flags);
}

extern void ksu_sucompat_init(void);
extern void ksu_sucompat_exit(void);
extern void ksu_ksud_init(void);
extern void ksu_ksud_exit(void);

int __init kernelsu_init(void)
{
#ifdef CONFIG_KSU_DEBUG
    pr_alert("*************************************************************");
    pr_alert("**         NOTICE: KernelSU running in DEBUG mode         **");
    pr_alert("*************************************************************");
#endif

    ksu_core_init();

    ksu_workqueue = alloc_ordered_workqueue("kernelsu_work_queue", 0);

    ksu_allowlist_init();
    ksu_throne_tracker_init();

#ifdef CONFIG_KPROBES
    ksu_sucompat_init();
    ksu_ksud_init();
#else
    pr_alert("KPROBES disabled, KernelSU may not fully work.");
#endif

    ksu_device_create();

    return 0;
}

void __exit kernelsu_exit(void)
{
    destroy_workqueue(ksu_workqueue);

#ifdef CONFIG_KPROBES
    ksu_ksud_exit();
    ksu_sucompat_exit();
#endif

    ksu_allowlist_exit();
    ksu_throne_tracker_exit();

    ksu_core_exit();
    misc_deregister(&ksu_misc_device);
}

module_init(kernelsu_init);
module_exit(kernelsu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("weishu");
MODULE_DESCRIPTION("Android KernelSU - no direct vfs_mount, safe for system apps");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
