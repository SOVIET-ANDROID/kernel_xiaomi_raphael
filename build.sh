#!/bin/bash
rm .version
# Bash Color
green='\033[01;32m'
red='\033[01;31m'
blink_red='\033[05;31m'
restore='\033[0m'

clear

# Resources
export CLANG_PATH=~/tc/neutron-clang/bin
export PATH=${CLANG_PATH}:${PATH}
#export CROSS_COMPILE=${CLANG_PATH}/aarch64-linux-gnu-
#export CROSS_COMPILE_ARM32=${CLANG_PATH}/arm-linux-gnueabi-
export THINLTO_CACHE=~/ltocache/
DEFCONFIG="raphael_defconfig"

# Kernel Details
VER="R4.1-STANDALONE"

# Paths
KERNEL_DIR=`pwd`
REPACK_DIR=~/AnyKernel3
ZIP_MOVE=~/AK-releases

# Functions
function clean_all {
		rm -rf $REPACK_DIR/Image* $REPACK_DIR/dtbo.img
		cd $KERNEL_DIR
		echo
		make clean && make mrproper
}

function make_kernel {
		echo
		make LLVM=1 LLVM_IAS=1 CC="ccache clang" $DEFCONFIG
		make LLVM=1 LLVM_IAS=1 CC="ccache clang" -j$(grep -c ^processor /proc/cpuinfo)

}

function make_zip {
                cp out/arch/arm64/boot/Image.gz-dtb $REPACK_DIR
                cp out/arch/arm64/boot/dtbo.img $REPACK_DIR
		cd $REPACK_DIR
		zip -r9 `echo $ZIP_NAME`.zip *
		mv  `echo $ZIP_NAME`*.zip $ZIP_MOVE
		cd $KERNEL_DIR
}


DATE_START=$(date +"%s")


echo -e "${green}"
echo "-----------------"
echo "Making Kernel:"
echo "-----------------"
echo -e "${restore}"


# Vars
BASE_AK_VER="SOVIET-STAR-K20P-"
DATE=`date +"%Y%m%d-%H%M"`
AK_VER="$BASE_AK_VER$VER"
ZIP_NAME="$AK_VER"-"$DATE"
#export LOCALVERSION=~`echo $AK_VER`
#export LOCALVERSION=~`echo $AK_VER`
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_USER=NATO66613
export KBUILD_BUILD_HOST=KREMLIN

echo

while read -p "Do you want to clean stuffs (y/n)? " cchoice
do
case "$cchoice" in
	y|Y )
		clean_all
		echo
		echo "All Cleaned now."
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to build?" dchoice
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
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done


echo -e "${green}"
echo "-------------------"
echo "Build Completed in:"
echo "-------------------"
echo -e "${restore}"

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo "Time: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
echo
