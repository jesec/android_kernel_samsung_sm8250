ifeq ($(CONFIG_OPPO_CHARGER_MTK),y)
subdir-ccflags-y += -Werror
subdir-ccflags-y += -Wno-unused-variable
ifeq ($(CONFIG_OPPO_CHARGER_MTK6779Q),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif
endif

obj-y      += charger_ic/
obj-y      += gauge_ic/
obj-y      += vooc_ic/
obj-y      += adapter_ic/

obj-y	+= oppo_charger.o
obj-y	+= oppo_gauge.o
obj-y	+= oppo_vooc.o
obj-y	+= oppo_adapter.o
obj-y	+= oppo_short.o