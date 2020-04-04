/*opyright (C), 2004-2019, OPPO Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: - oppo_apps_power_monitor.h
** Description: Network interaction monitoring system.
**
** Version: 1.0
** Date : 2019/09/29
** Author: liuwei@TECH.NW.DATA
**
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** liuwei 2019/09/29 1.0 build this module
****************************************************************/

#ifndef _OPPO_APPS_POWER_MONITOR_H
#define _OPPO_APPS_POWER_MONITOR_H

#include <linux/types.h>
#include <linux/err.h>
#include <linux/netlink.h>



int app_monitor_dl_ctl_msg_handle(struct nlmsghdr *nlh);
int app_monitor_dl_report_msg_handle(struct nlmsghdr *nlh);
void oppo_app_power_monitor_init(void);
void oppo_app_power_monitor_fini(void);

#endif /* _OPPO_APPS_POWER_MONITOR_H */
