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
KERNEL_MAKE_ENV="VARIANT_DEFCONFIG=vendor/variant_$1_defconfig"

case $2 in
  savedefconfig)
    SAVE_DEFCONFIG="savedefconfig"
    ;;

  "")
    ;;

  *)
    KERNEL_MAKE_ENV="$KERNEL_MAKE_ENV LOCALVERSION=-$2 DEBUG_DEFCONFIG=vendor/release_defconfig"
    ;;
esac

make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE CROSS_COMPILE_COMPAT=$BUILD_CROSS_COMPILE_COMPAT LLVM=1 LLVM_IAS=1 CLANG_TRIPLE=$CLANG_TRIPLE vendor/x1q_chn_openx_defconfig || exit 1
make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE CROSS_COMPILE_COMPAT=$BUILD_CROSS_COMPILE_COMPAT LLVM=1 LLVM_IAS=1 CLANG_TRIPLE=$CLANG_TRIPLE $SAVE_DEFCONFIG || exit 1

if [[ ! -z "$SAVE_DEFCONFIG" ]]; then
cp $(pwd)/out/defconfig $(pwd)/arch/$ARCH/configs/vendor/x1q_chn_openx_defconfig
exit 0
fi

cp $(pwd)/out/arch/$ARCH/boot/Image $(pwd)/out/Image

DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-*-r*.dtbo)

cat ${DTS_DIR}/vendor/qcom/*.dtb > $(pwd)/out/dtb.img
$(pwd)/tools/mkdtimg create $(pwd)/out/dtbo.img --page_size=4096 ${DTBO_FILES}
