#!/sbin/sh
# KernelZip Installation Script
# Author: stitchneno @ GitHub

## KernelZip Configuration
properties() { '
kernel.string=neno kernel by stitchneno @ github
do.modules=0
do.cleanup=1
do.cleanuponabort=0
device.name1=raphael
device.name2=raphaelin
supported.versions=
'; } # end properties


## Configurable Paths
boot_partition=/dev/block/bootdevice/by_name/boot;
is_slot_device=0;
# Set the ramdisk compression (auto should work in most cases)
ramdisk_compression=auto;

## KernelZip methods (DO NOT CHANGE)
. tools/core.sh;

## Function to set boot attributes
# Change file permissions and ownership in the ramdisk
chmod -R 750 $ramdisk/*;
chown -R root:root $ramdisk/*;

# AnyKernel install
# Dump the boot image
dump_boot;

# Write the modified boot image
write_boot;



