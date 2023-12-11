# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -mllvm -inline-threshold=15000
ccflags-y += -mllvm -inlinehint-threshold=10000
ccflags-y += -O3

zram-y	:=	zcomp.o zram_drv.o
zram-$(CONFIG_ZRAM_DEDUP)	+=	zram_dedup.o

obj-$(CONFIG_ZRAM)	+=	zram.o
