#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Define the default configuration file
DEFAULT_CONFIG="./arch/${ARCH}/configs/$DEFCONFIG"

# Check if DEBUGFS is to be disabled
if [ "${DISABLE_DEBUGFS}" == "true" ]; then
    echo "Build variant: ${TARGET_BUILD_VARIANT}"

    if [ "${TARGET_BUILD_VARIANT}" == "user" ] && [ "${ARCH}" == "arm64" ]; then
        echo "Combining fragments for user build"

        (cd "${KERNEL_DIR}" && \
        ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" \
        ./scripts/kconfig/merge_config.sh \
        "${DEFAULT_CONFIG}" \
        "./arch/${ARCH}/configs/vendor/debugfs.config"
        make "${MAKE_ARGS}" ARCH="${ARCH}" \
        CROSS_COMPILE="${CROSS_COMPILE}" savedefconfig
        mv defconfig "./arch/${ARCH}/configs/$DEFCONFIG"
        rm .config)
    else
        if [[ "${DEFCONFIG}" == *"perf_defconfig" ]] && [ "${ARCH}" == "arm64" ]; then
            echo "Resetting perf defconfig"
            (cd "${KERNEL_DIR}" && \
            git checkout "arch/${ARCH}/configs/$DEFCONFIG")
        fi
    fi
fi
