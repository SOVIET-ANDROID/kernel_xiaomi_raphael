#
# Makefile for the FUSE filesystem.
#

ccflags-y += -mllvm -inline-threshold=15000
ccflags-y += -mllvm -inlinehint-threshold=10000

obj-$(CONFIG_FUSE_FS) += fuse.o
obj-$(CONFIG_CUSE) += cuse.o

fuse-objs := dev.o dir.o file.o inode.o control.o xattr.o acl.o
fuse-objs += passthrough.o
