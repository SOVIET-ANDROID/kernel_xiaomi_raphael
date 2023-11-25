#!/bin/bash
# Bash Color
green='\033[01;32m'
red='\033[01;31m'
blink_red='\033[05;31m'
restore='\033[0m'

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
EDITION="legacy"
VER="$EDITION"

# Vars
BASE_AK_VER="SOVIET-STAR-K20P-"
DATE=`date +"%Y%m%d-%H%M"`
AK_VER="$BASE_AK_VER$VER"
ZIP_NAME="$AK_VER"-"$DATE"
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_USER=INHPKUL007
export KBUILD_BUILD_HOST=SAKSHAM

# Paths
KERNEL_DIR=`pwd`
REPACK_DIR=~/ziptool
ZIP_MOVE=~/

# Functions
clean_all() {
		rm -rf $REPACK_DIR/Image* $REPACK_DIR/dtbo.img
		cd $KERNEL_DIR
		make mrproper
}

make_kernel() {
		make LLVM=1 LLVM_IAS=1 CC="ccache clang" $DEFCONFIG
		make LLVM=1 LLVM_IAS=1 CC="ccache clang" -j$(grep -c ^processor /proc/cpuinfo) \
		| grep -E "warning|error:" | 2> build.log
}

make_zip() {
        cp out/arch/arm64/boot/Image.gz-dtb $REPACK_DIR
        cp out/arch/arm64/boot/dtbo.img $REPACK_DIR
		cd $REPACK_DIR
		zip -r9 `echo $ZIP_NAME`.zip *
		mv  `echo $ZIP_NAME`*.zip $ZIP_MOVE
		cd $KERNEL_DIR
}

sideload() {
	cd $HOME
	adb sideload SOVIET-STAR-K20P-legacy-*.zip
	cd $PWD
}

removezip() {
	cd $HOME
	rm -rf SOVIET-STAR-K20P-legacy-*.zip
	cd $PWD
}

DATE_START=$(date +"%s")


print_ui "-----------------"
print_ui "  Making Kernel  "
print_ui "-----------------"


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
		print_ui "Invalid try again!"
		;;
esac
done

while read -p "Do you want to adb sideload (y/n)? " aschoice
do 	
	case "aschoice" in 
		y|Y )
			sideload 
			print_ui "Sideload Completed..."
			break
			;;
		n|N )
			break
			;;
		* ) 
			print_ui "Invalid try again!"
	esac
done

while read -p "Do you want to remove zip (y/n)? " rchoice
do 
	case "rchoice" in	
		y|Y ) 
			removezip
			print_ui "Zipfile have been removed..."
			break
			;;
		n|N )
			break
			;;
		* ) 
			print_ui "Invalid try again!"
	esac
done

print_ui "-------------------"
print_ui "  Build Completed  "
print_ui "-------------------"


DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
print_ui "Time: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."

