#!/bin/bash

CHIPSET_NAME=kona
VARIANT=$1
REGION=$2

export ARCH=arm64
mkdir -p out

DTS_DIR=$(pwd)/out/arch/$ARCH/boot/dts
DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-*-r*.dtbo 2>/dev/null)
rm -f ${DTS_DIR}/vendor/qcom/*.dtb ${DTBO_FILES}

BUILD_CROSS_COMPILE=aarch64-linux-gnu-
BUILD_CROSS_COMPILE_COMPAT=arm-linux-gnueabi-
CLANG_TRIPLE=aarch64-linux-gnu-

KERNEL_MAKE_ENV="ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE CROSS_COMPILE_COMPAT=$BUILD_CROSS_COMPILE_COMPAT LLVM=1 CLANG_TRIPLE=$CLANG_TRIPLE"
KERNEL_MAKE_ENV="$KERNEL_MAKE_ENV VARIANT_DEFCONFIG=vendor/$1/kona_sec_$1_$2_defconfig"

case $3 in
    "")
    ;;

    *)
        KERNEL_MAKE_ENV="$KERNEL_MAKE_ENV LOCALVERSION=-$3 DEBUG_DEFCONFIG=vendor/release_defconfig"
    ;;
esac

make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV vendor/kona_sec_defconfig || exit 1
make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV || exit 1

cp $(pwd)/out/arch/$ARCH/boot/Image $(pwd)/out/Image

DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-*-r*.dtbo)

cat ${DTS_DIR}/vendor/qcom/*.dtb > $(pwd)/out/dtb.img
$(pwd)/tools/mkdtimg create $(pwd)/out/dtbo.img --page_size=4096 ${DTBO_FILES}
