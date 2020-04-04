ccflags-y += -DBSP_SIL_PLAT_QCOM
ifeq ($(TARGET_PRODUCT),msmnile)
ccflags-y += -DUSED_GPIO_PWR
ccflags-y += -DCONFIG_MSM_855
endif

ifeq ($(CONFIG_OPPO_FINGERPRINT_QCOM),y)
obj-y += silead_fp_platform.o
else ifeq ($(CONFIG_OPPO_FINGERPRINT_MTK),y)
#obj-y += silead_fp_platform.o
#reserved for mtk.
endif
