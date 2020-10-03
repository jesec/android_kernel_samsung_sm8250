define __sec_dtbo_build
dtbo-$(2) += $(1)
$(1)-base := $(3)
endef

define sec_dtbo_build
$(foreach dtbo, $(1), $(eval $(call __sec_dtbo_build, $(dtbo),$(2),$(3))))
endef

QCOM_DTB := ../../vendor/qcom
SEC_KONA_BASE_DTB := $(QCOM_DTB)/kona.dtb $(QCOM_DTB)/kona-v2.dtb $(QCOM_DTB)/kona-v2.1.dtb

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)

SEC_C2Q_USA_DTBO := \
    kona-sec-c2q-usa-overlay-r00.dtbo \
	kona-sec-c2q-usa-overlay-r01.dtbo \
	kona-sec-c2q-usa-overlay-r02.dtbo \
	kona-sec-c2q-usa-overlay-r03.dtbo \
	kona-sec-c2q-usa-overlay-r04.dtbo \
	kona-sec-c2q-usa-overlay-r06.dtbo \
	kona-sec-c2q-usa-overlay-r07.dtbo \
	kona-sec-c2q-usa-overlay-r08.dtbo \
	kona-sec-c2q-usa-overlay-r09.dtbo \
	kona-sec-c2q-usa-overlay-r10.dtbo

$(eval $(call sec_dtbo_build, $(SEC_C2Q_USA_DTBO),$(CONFIG_MACH_C2Q_USA_SINGLE),$(SEC_KONA_BASE_DTB)))

endif
