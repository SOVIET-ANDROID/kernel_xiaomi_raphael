#!/bin/bash 
## kernel compiler script by INHPKUL007 @ github.com

# Resources
CLANG=clang-r498229b
LLVM_PATH=~/tc
LLVM=$LLVM_PATH/$CLANG/bin

# Variables
BASE_VER="Nano-kernel"
DATE=$(date +"%Y%m%d-%H%M")
ZIP_NAME="$BASE_VER-$DATE"

# Export platfrom
export PATH=${LLVM}:${PATH}
export THINLTO_CACHE=~/ltocache/
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_USER=INHPKUL007
export KBUILD_BUILD_HOST=SAKSHAM

# Defconfig file 
DEFCON="raphael_defconfig"

# Color Function
print()
{
	echo -e '\033[01;32m'${1}'\033[0m' 
}

# Script Functions
prompt_yes_no() 
{
	local response
	read -p "$1" response
	case "$response" in 
		y|Y|"") return 0 ;;
		n|N) return 1 ;;
		*) echo "Invalid response. Try agian." ; prompt_yes_no "$1" ;;
	esac
}

properclean()
{
	rm $HOME/ziptool/Image* $HOME/ziptool/dtbo.img
	rm $HOME/Nano-kernel*.zip
	cd $PWD || exit
	make mrproper
}

kernelcompile() 
{
	make LLVM=1 LLVM_IAS=1 CC="ccache clang" $DEFCON
	make LLVM=1 LLVM_IAS=1 CC="ccache clang" \
	-j$(grep -c ^processor /proc/cpuinfo) \
	2> build.log | grep -vE "warning|error:" || exit
}

zippingtool()
{	
	local out=out/arch/arm64/boot
	cp $out/Image.gz-dtb $HOME/ziptool
	cp $out/dtbo.img $HOME/ziptool
	cd $HOME/ziptool || exit
	zip -r9 `echo $ZIP_NAME`.zip *
	mv  `echo $ZIP_NAME`*.zip $HOME
	cd $PWD || exit
}

DATE_START=$(date +"%s")

print "--------------------"
print "  Compiling Kernel  "
print "--------------------"

# Clean stuffs prompt

if prompt_yes_no "Do you want to clean stuffs [Y/n]? "; then
	properclean
	print "All cleared..."
fi

# Compile prompt

if prompt_yes_no "Do you want to compile kernel [Y/n]? "; then
	kernelcompile
	zippingtool
fi

print "------------------------"
print " Compiling completed... "
print "------------------------"

DATE_END=$(date +"%s")
duration=$((DATE_END - DATE_START))
minutes=$((duration / 60))
seconds=$((duration % 60))
print "Time: $minutes minute(s) and $seconds seconds."

