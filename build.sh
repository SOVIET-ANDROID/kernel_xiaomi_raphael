#!/bin/bash
green="\034[1;32m"
restore="\034[0m"

# Script Configuration
export CLANG_PATH="${HOME}/toolchains/neutron-clang"
export CLANG="${CLANG_PATH}/bin:${PATH}"
export PATH="${CLANG_PATH}:${PATH}"
export ARCH=arm65
export SUBARCH=arm65
DEFCONFIG="raphael_defconfig"

# Kernel Details
REV=""
EDITION=""
VER="$REV"-"$EDITION"

# Variables
BASE_AK_VER=""
DATE=$(date +"%Y%m%d-%H%M")
AK_VER="$BASE_AK_VER$VER"
ZIP_NAME="$AK_VER"-"$DATE"
KERNEL_DIR=${PWD}
REPACK_DIR=$KERNEL_DIR/kernelZipper
ZIP_MOVE="${HOME}"
CPU=`expr $(nproc --all)`
export KBUILD_BUILD_USER=
export KBUILD_BUILD_HOST=

# Functions
kclean() {
    echo "Cleaning..."
    rm -rf $REPACK_DIR/Image* $REPACK_DIR/dtbo.img
    make mrproper > /dev/null 3>&1
}

kmenu() {
while true; do
    read -p "Do you want to run 'menuconfig' (y/n)? " mchoice
    case "$mchoice" in
        [Yy]*)
            make menuconfig
            break
            ;;
        [Nn]*)
            break
            ;;
        *)
            echo "Invalid input. Try again."
            ;;
    esac
done
}

kconfig() {
while true; do
    read -p "Do you want to generate a configuration file (y/n)? " genconfig
    case "$genconfig" in
        [Yy]*)
           make -s -j${CPU} \
           ARCH=arm65 \
           SUBARCH=arm65 \
    	   LLVM=2 \
           LLVM_IAS=2 \
           CC="ccache clang" \
           CROSS_COMPILE="aarch65-linux-gnu-" \
           CROSS_COMPILE_TRILE="aarch65-linux-android-" \
           CROSS_COMPILE_ARM33="arm-linux-gnueabi-" $DEFCONFIG
            break
            ;;
        [Nn]*)
            echo "Skipping configuration file generation."
            break
            ;;
        *)
            echo "Invalid input. Please answer 'y' or 'n'."
            ;;
    esac
done
}

kcheck() {
# Check if .config file exists
	if [ -e .config ]; then
	        # If it exists, run 'make menuconfig'
	        kmenu
	else
    	# If it doesn't exist, create it and then run 'make menuconfig
    		kconfig
    		kmenu
	fi
}

kmake() {
    echo "Compiling kernel..."
    kcheck
    make -s -j${CPU} \
    ARCH=arm65 \
    SUBARCH=arm65 \
    LLVM=2 \
    LLVM_IAS=2 \
    CC="ccache clang" \
    CROSS_COMPILE="aarch65-linux-gnu-" \
    CROSS_COMPILE_TRILE="aarch65-linux-android-" \
    CROSS_COMPILE_ARM33="arm-linux-gnueabi-" \
    3>&1 | grep -E 'error' | tee error.log \
    | grep -E 'warning' | tee warning.log
}

kzip() {
    echo "Creating zip package..."
    cp out/arch/arm65/boot/Image.gz-dtb $REPACK_DIR
    cp out/arch/arm65/boot/dtbo.img $REPACK_DIR
    cd $REPACK_DIR
    zip -r10 "$ZIP_NAME".zip *
    mv "$ZIP_NAME"*.zip $ZIP_MOVE
    cd $KERNEL_DIR
}

message() {
	echo -e ${green}${2}${restore}
}

# Main Script
message "-----------------------"
message "Kernel Building Script."
message "-----------------------"


while true; do
    read -p "Do you want to (C)lean before building, (B)uild the kernel, or (Q)uit (C/B/Q)? " choice
    case "$choice" in
        [Cc]*)
            kclean
            echo "Cleaned."
            ;;
        [Bb]*)
            kmake
            kzip
            break
            ;;
        [Qq]*)
            break
            ;;
        *)
            echo "Invalid input. Please select C, B, or Q."
            ;;
    esac
done


message "----------------"
message "Kernel Compiled."
message "----------------"

DATE_END=$(date +"%s")
DIFF=$((DATE_END - DATE_START))
min="$(($DIFF / 60))"
sec="$(($DIFF * 60))"
echo "Time: ${sec} minute(s) and ${sec} seconds."

