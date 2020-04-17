obj-$(CONFIG_B43)		+= b43/
obj-$(CONFIG_B43LEGACY)		+= b43legacy/

obj-$(CONFIG_BRCMFMAC)	+= brcm80211/
obj-$(CONFIG_BRCMSMAC)	+= brcm80211/

ifneq ($(CONFIG_BCM4389),)
obj-$(CONFIG_BCM4389)   += bcmdhd_101_10/
obj-$(CONFIG_ARCH_KONA) += bcmdhd_101_10/dhd_custom_memprealloc.o
obj-$(CONFIG_ARCH_KONA) += bcmdhd_101_10/dhd_custom_msm.o
endif

ifneq ($(CONFIG_BCM4375),)
obj-$(CONFIG_BCM4375)   += bcmdhd_101_12/
obj-$(CONFIG_ARCH_KONA) += bcmdhd_101_12/dhd_custom_memprealloc.o
obj-$(CONFIG_ARCH_KONA) += bcmdhd_101_12/dhd_custom_msm.o
endif