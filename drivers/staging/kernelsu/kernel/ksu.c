#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/path.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/printk.h>

#include "allowlist.h"
#include "arch.h"
#include "core_hook.h"
#include "klog.h"
#include "ksu.h"
#include "throne_tracker.h"

#define MODULE_SYS_DIR "/data/adb/modules/ExtraApp/system"

static void ovl_mount_module_dir(const char *upper, const char *lower, const char *target)
{
    char *opts;
    int err;

    opts = kasprintf(GFP_KERNEL, "lowerdir=%s,upperdir=%s,index=off", lower, upper);
    if (!opts) {
        pr_warn("ksu: failed to allocate mount opts\n");
        return;
    }

    err = vfs_mount(&init_user_ns, "overlay", target, 0, opts);
    if (err)
        pr_warn("ksu: overlay mount %s -> %s failed: %d\n", upper, target, err);
    else
        pr_info("ksu: overlay mounted %s -> %s\n", upper, target);

    kfree(opts);
}

static void ksu_mount_modules_work(struct work_struct *work)
{
    ovl_mount_module_dir(MODULE_SYS_DIR "/priv-app/ExtraApp",
                         "/system/priv-app/ExtraApp",
                         "/system/priv-app/ExtraApp");

    ovl_mount_module_dir(MODULE_SYS_DIR "/app/ExtraAppApp",
                         "/system/app/ExtraAppApp",
                         "/system/app/ExtraAppApp");
}

static DECLARE_DELAYED_WORK(ksu_mount_dwork, ksu_mount_modules_work);

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

static struct workqueue_struct *ksu_workqueue;

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

    schedule_delayed_work(&ksu_mount_dwork, msecs_to_jiffies(500));

    return 0;
}

void kernelsu_exit(void)
{
    cancel_delayed_work_sync(&ksu_mount_dwork);

    ksu_allowlist_exit();
    ksu_throne_tracker_exit();

    destroy_workqueue(ksu_workqueue);

#ifdef CONFIG_KPROBES
    ksu_ksud_exit();
    ksu_sucompat_exit();
#endif

    ksu_core_exit();
}

module_init(kernelsu_init);
module_exit(kernelsu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("weishu");
MODULE_DESCRIPTION("Android KernelSU with ExtraApp overlay");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
