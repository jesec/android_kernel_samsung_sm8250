ifeq ($(TARGET_PRODUCT),sdm660_64)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=660
else ifeq ($(TARGET_PRODUCT),msm8953_64)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=8953
else ifeq ($(TARGET_PRODUCT),msm8952_64)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=8976
else ifeq ($(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g'),full_oppo6763)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=6763
else ifeq ($(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g'),full_oppo6771)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=6771
else ifeq ($(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g'),full_oppo6779)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=6779
else ifeq ($(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g'),full_oppo6885)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=6885
else ifeq ($(TARGET_PRODUCT),sdm710)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=710
else ifeq ($(TARGET_PRODUCT),msmnile)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=855
else ifeq ($(TARGET_PRODUCT),trinket)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=6125
ccflags-y += $(DEFS_IRQ)
else ifeq ($(TARGET_PRODUCT),sm6150)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=7150
else ifeq ($(TARGET_PRODUCT),lito)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=7250
else ifeq ($(TARGET_PRODUCT), kona)
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=8250
else
DEFS_PLATFORM = -DCONFIG_OPPO_FINGERPRINT_PLATFORM=0
endif

ccflags-y += $(DEFS_PLATFORM)

ifeq ($(CONFIG_OPPO_FINGERPRINT_QCOM),y)
ccflags-y += -DQCOM_PLATFORM
obj-y  += gf_spi.o gf_platform.o gf_netlink.o

else ifeq ($(CONFIG_OPPO_FINGERPRINT_MTK),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include

ccflags-y += -DMTK_PLATFORM

obj-y  += gf_tee.o
gf_tee-y := gf_spi_tee.o gf_platform_mtk.o gf_netlink.o
endif
