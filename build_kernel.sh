#!/bin/bash

CHIPSET_NAME=kona
MODEL=$1
REGION=$2
CARRIER=$3

export ARCH=arm64
mkdir out

BUILD_CROSS_COMPILE=aarch64-linux-gnu-
CLANG_TRIPLE=aarch64-linux-gnu-
KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

if [[ $4 == "lto" ]]; then KERNEL_MAKE_ENV="$KERNEL_MAKE_ENV VARIANT_DEFCONFIG=vendor/lto_defconfig"; fi

make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE CC=${KERNEL_LLVM_BIN}clang LD=${KERNEL_LLVM_BIN}ld.lld NM=${KERNEL_LLVM_BIN}llvm-nm AR=${KERNEL_LLVM_BIN}llvm-ar OBJCOPY=${KERNEL_LLVM_BIN}llvm-objcopy OBJDUMP=${KERNEL_LLVM_BIN}llvm-objdump CLANG_TRIPLE=$CLANG_TRIPLE vendor/${MODEL}_${REGION}_${CARRIER}_defconfig || exit 1
make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE CC=${KERNEL_LLVM_BIN}clang LD=${KERNEL_LLVM_BIN}ld.lld NM=${KERNEL_LLVM_BIN}llvm-nm AR=${KERNEL_LLVM_BIN}llvm-ar OBJCOPY=${KERNEL_LLVM_BIN}llvm-objcopy OBJDUMP=${KERNEL_LLVM_BIN}llvm-objdump CLANG_TRIPLE=$CLANG_TRIPLE || exit 1

cp $(pwd)/out/arch/$ARCH/boot/Image $(pwd)/out/Image

DTS_DIR=$(pwd)/out/arch/$ARCH/boot/dts
DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-${MODEL}-${REGION}-overlay*.dtbo)

cat ${DTS_DIR}/vendor/qcom/*.dtb > $(pwd)/out/dtb.img
$(pwd)/tools/mkdtimg create $(pwd)/out/dtbo.img --page_size=4096 ${DTBO_FILES}
