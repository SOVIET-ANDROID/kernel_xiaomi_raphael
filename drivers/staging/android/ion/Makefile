# SPDX-License-Identifier: GPL-2.0
ccflags-y += -mllvm -inline-threshold=15000
ccflags-y += -mllvm -inlinehint-threshold=10000
obj-$(CONFIG_ION) +=	ion.o ion_heap.o \
			ion_page_pool.o ion_system_heap.o \
			ion_carveout_heap.o ion_chunk_heap.o \
			ion_system_secure_heap.o ion_cma_heap.o \
			ion_secure_util.o ion_cma_secure_heap.o msm/

