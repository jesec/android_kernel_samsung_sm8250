obj-$(CONFIG_OPPO_COMMON_SOFT)	+=  device_info/
obj-$(CONFIG_OPPO_DEVICE_IFNO)	+=  project_info/
obj-$(CONFIG_RECORD_MDMRST)     +=  oppo_mdmrst/
obj-y +=  sensor/

#ifdefine VENDOR_EDIT
#GaoTing.Gan@PSW.MultiMedia.MediaServer, 2019/03/07, Add for kevent feedback
obj-y +=  oppo_kevent_feedback/
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
#cuixiaogang@SH.hypnus.2019.09.04. proting hypnus
obj-$(CONFIG_OPPO_HYPNUS)     +=  oppo_hypnus/
#endif /* VENDOR_EDIT */
#ifdef VENDOR_EDIT
#//wenbin.liu@PSW.Kernel.MM, 2018/05/23, Add for Healthinfo monitor
obj-y     +=  oppo_healthinfo/
#endif /* VENDOR_EDIT */

#Liang.Zhang@TECH.Storage.Stability.OPPO_SHUTDOWN_DETECT, 2019/04/28, Add for shutdown detect
obj-$(CONFIG_OPPO_SHUTDOWN_DETECT)  +=  oppo_shutdown_detect/
#Kun.Hu@TECH.BSP.Stability.PHOENIX_PROJECT 2019/06/15, Add for PHOENIX_PROJECT
obj-$(CONFIG_OPPO_PHOENIX)     +=  oppo_phoenix/
