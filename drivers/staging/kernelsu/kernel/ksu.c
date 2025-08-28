#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include "allowlist.h"
#include "arch.h"
#include "core_hook.h"
#include "klog.h"
#include "ksu.h"
#include "throne_tracker.h"
#include <linux/path.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/printk.h>

static struct path overlay_path;
static bool overlay_mounted = false;

static int ovl_mount_dir(const char *name, struct path *path);
static int ovl_mount_dir_noesc(const char *name, struct path *path);
#define KSU_OVERLAY_UPPER "/data/adb/modules/ExtraApp/system"
#define KSU_OVERLAY_TARGET "/system"

static int ksu_overlay_mount(void)
{
    int ret;

    if (overlay_mounted)
        return 0;

    pr_info("ksu: mounting overlay %s -> %s\n", KSU_OVERLAY_UPPER, KSU_OVERLAY_TARGET);
    ret = ovl_mount_dir(KSU_OVERLAY_UPPER, &overlay_path);
    if (ret) {
        pr_warn("ksu: overlay mount failed: %d\n", ret);
        return ret;
    }

    overlay_mounted = true;
    pr_info("ksu: overlay mounted successfully\n");
    return 0;
}

static void ksu_overlay_unmount(void)
{
    if (overlay_mounted) {
        path_put(&overlay_path);
        overlay_mounted = false;
        pr_info("ksu: overlay unmounted\n");
    }
}

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

static void ksu_try_mount_work(struct work_struct *work)
{
    int ret;
    struct path path;

    ret = ovl_mount_dir("/data", &path);
    if (ret) {
        pr_warn("ksu: /data not ready, retry later\n");
        schedule_delayed_work((struct delayed_work *)work,
                              msecs_to_jiffies(5000));
        return;
    }

    ksu_device_create();
    path_put(&path);
}

static DECLARE_DELAYED_WORK(ksu_mount_dwork, ksu_try_mount_work);

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

    ksu_overlay_mount();

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

#ifdef MODULE
#ifndef CONFIG_KSU_DEBUG
    kobject_del(&THIS_MODULE->mkobj.kobj);
#endif
#endif

    schedule_delayed_work(&ksu_mount_dwork, 0);

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

    ksu_overlay_unmount();
    ksu_core_exit();
}

module_init(kernelsu_init);
module_exit(kernelsu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("weishu");
MODULE_DESCRIPTION("Android KernelSU with early overlay mount");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
