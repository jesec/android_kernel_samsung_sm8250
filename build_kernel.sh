#!/bin/bash

CHIPSET_NAME=kona
MODEL=$1
REGION=$2
CARRIER=$3

export ARCH=arm64
mkdir out

BUILD_CROSS_COMPILE=$ANDROID_TOP/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
KERNEL_LLVM_BIN=$ANDROID_TOP/prebuilts/clang/host/linux-x86/clang-r353983d/bin/clang
CLANG_TRIPLE=aarch64-linux-gnu-
KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

make -j8 -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE vendor/${MODEL}_${REGION}_${CARRIER}_defconfig || exit 1
make -j8 -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE || exit 1

cp $(pwd)/out/arch/$ARCH/boot/Image $(pwd)/out/Image

DTS_DIR=$(pwd)/out/arch/$ARCH/boot/dts
DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-${MODEL}-${REGION}-overlay*.dtbo)

cat ${DTS_DIR}/vendor/qcom/*.dtb > $(pwd)/out/dtb.img
$(pwd)/tools/mkdtimg create $(pwd)/out/dtbo.img --page_size=4096 ${DTBO_FILES}
