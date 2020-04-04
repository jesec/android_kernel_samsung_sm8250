ccflags-$(CONFIG_ARCH_QCOM) += -I$(srctree)/drivers/gpu/msm/ -I$(srctree)/kernel/sched/

hypnus_ops-y := hypnus_msm_op.o

oppo_hypnus-objs := hypnus_main.o hypnus_dev.o hypnus_sysfs.o hypnus_display.o
oppo_hypnus-objs += $(hypnus_ops-y)

obj-$(CONFIG_OPPO_HYPNUS) += oppo_hypnus.o
