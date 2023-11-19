#!/bin/bash
# Bash Color
green='\033[01;32m'
red='\033[01;31m'
restore='\033[0m'

clear

print() {
	echo -e ${green}${1}${restore}
}

print_ui() {
	echo -e ${red}${1}${restore}
}

# Resources
export CLANG_PATH=~/tc/aosp/clang-r498229b/bin
export PATH=${CLANG_PATH}:${PATH}
export THINLTO_CACHE=~/ltocache/
DEFCONFIG="raphael_defconfig"

# Kernel Details
REV="V1"
EDITION="legacy"
VER="$REV"-"$EDITION"

# Vars
BASE_AK_VER="NENO-KERNEL-"
DATE=`date +"%Y%m%d-%H%M"`
AK_VER="$BASE_AK_VER$VER"
ZIP_NAME="$AK_VER"-"$DATE"
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_USER=INHPKUL007
export KBUILD_BUILD_HOST=SAKSHAM

# Paths
KERNEL_DIR=$PWD
REPACK_DIR=$HOME/ziptool
ZIP_MOVE=$HOME

# Functions
clean_all() {
		rm -rf $REPACK_DIR/Image* $REPACK_DIR/dtbo.img
		cd $KERNEL_DIR
		make mrproper
}

make_kernel() {
		make LLVM=1 LLVM_IAS=1 CC="ccache clang" $DEFCONFIG
		make LLVM=1 LLVM_IAS=1 CC="ccache clang" -j$(grep -c ^processor /proc/cpuinfo) \
		2> build.log | grep -E "error|warning:"
}

make_zip() {
        cp out/arch/arm64/boot/Image.gz-dtb $REPACK_DIR
        cp out/arch/arm64/boot/dtbo.img $REPACK_DIR
		cd $REPACK_DIR
		zip -r9 `echo $ZIP_NAME`.zip *
		mv  `echo $ZIP_NAME`*.zip $ZIP_MOVE
		cd $KERNEL_DIR
}

DATE_START=$(date +"%s")


print "------------------"
print "  Making Kernel:  "
print "------------------"

while read -p "Do you want to clean stuffs (y/n)? " cchoice
do
case "$cchoice" in
	y|Y )
		clean_all
		print_ui "All Cleaned now."
		break
		;;
	n|N )
		break
		;;
	* )
		print_ui "Invalid try again!"
		;;
esac
done

while read -p "Do you want to build (y/n)? " dchoice
do
case "$dchoice" in
	y|Y )
		make_kernel
        make_zip
		break
		;;
	n|N )
		break
		;;
	* )
		print_ui "Invalid try again!"
		;;
esac
done

print "-------------------"
print "Build Completed in:"
print "-------------------"

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
print_ui "Time: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
