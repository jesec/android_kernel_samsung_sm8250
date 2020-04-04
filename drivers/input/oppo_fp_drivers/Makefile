obj-$(CONFIG_OPPO_FINGERPRINT) += oppo_fp_common/

ifeq ($(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g'),full_oppo6885)
obj-$(CONFIG_OPPO_FINGERPRINT) += goodix_fp/
else
#obj-$(CONFIG_OPPO_FINGERPRINT) += fpc/
obj-$(CONFIG_OPPO_FINGERPRINT) += goodix_fp/
#obj-$(CONFIG_OPPO_FINGERPRINT) += silead/
endif

