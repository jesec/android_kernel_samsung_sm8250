/************************************************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: oppo_oppo_iface.c
**
** Description:
**		Definitions for oppo_iface.
**
** Version: 1.0
** Date created: 2018/01/14,20:27
** Author: Fei.Mo@PSW.BSP.Sensor
**
** --------------------------- Revision History: ------------------------------------
* <version>		<date>		<author>		<desc>
* Revision 1.0		2018/01/14	Fei.Mo@PSW.BSP.Sensor	Created
**************************************************************************************/
#ifndef __OPPO_IFACE_H__
#define __OPPO_IFACE_H__
#include <linux/alarmtimer.h>
#include <linux/workqueue.h>

#define ALS (0x01 << 0)
#define PROX (0x01 << 1)
#define REPORT_TIME		(200)//20ms

struct oppo_als_dc_info {
	int dc_actived;
	struct proc_dir_entry 		*proc_oppo_als;
};

struct oppo_iface_chip {
	struct device	*dev;
	int brightness;
	int dc_actived;
	int available_sensor;
	bool is_timeout;
	bool is_need_to_report;
	struct oppo_als_dc_info dc_info;
	struct alarm timer;
	struct work_struct	report_work;
};

#endif // __OPPO_IFACE_H__

