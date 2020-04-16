#!/bin/bash

CHIPSET_NAME=kona
VARIANT=$1

export ARCH=arm64
mkdir out

DTS_DIR=$(pwd)/out/arch/$ARCH/boot/dts
DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-*-r*.dtbo)
rm ${DTS_DIR}/vendor/qcom/*.dtb ${DTBO_FILES}

BUILD_CROSS_COMPILE=aarch64-linux-gnu-
BUILD_CROSS_COMPILE_COMPAT=arm-linux-gnueabi-
CLANG_TRIPLE=aarch64-linux-gnu-
KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"
KERNEL_MAKE_ENV="$KERNEL_MAKE_ENV VARIANT_DEFCONFIG=vendor/variant_$1_defconfig"

if [[ ! -z "$2" ]]
then
KERNEL_MAKE_ENV="$KERNEL_MAKE_ENV LOCALVERSION=-$2 DEBUG_DEFCONFIG=vendor/release_defconfig"
fi

make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE CROSS_COMPILE_COMPAT=$BUILD_CROSS_COMPILE_COMPAT LLVM=1 CLANG_TRIPLE=$CLANG_TRIPLE vendor/x1q_chn_openx_defconfig || exit 1
make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE CROSS_COMPILE_COMPAT=$BUILD_CROSS_COMPILE_COMPAT LLVM=1 CLANG_TRIPLE=$CLANG_TRIPLE || exit 1

cp $(pwd)/out/arch/$ARCH/boot/Image $(pwd)/out/Image

DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-*-r*.dtbo)

cat ${DTS_DIR}/vendor/qcom/*.dtb > $(pwd)/out/dtb.img
$(pwd)/tools/mkdtimg create $(pwd)/out/dtbo.img --page_size=4096 ${DTBO_FILES}
