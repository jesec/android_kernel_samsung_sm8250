/***********************************************************
** Copyright (C), 2008-2019, OPPO Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: oppo_apps_monitor.h
** Description: Add for apps network monitor
**
** Version: 1.0
** Date : 2019/06/14
** Author: Xiong.Li@TECH.CN.WiFi.Network.2022890,2019/06/14
**
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** lixiong 2019/06/14 1.0 build this module
****************************************************************/

#ifndef _OPPO_APPS_MONITOR_H
#define _OPPO_APPS_MONITOR_H

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/version.h>
#include <net/tcp.h>
#include <linux/random.h>
#include <net/sock.h>
#include <net/dst.h>
#include <linux/file.h>
#include <net/tcp_states.h>
#include <linux/netlink.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/netfilter/nf_queue.h>
#include <linux/netfilter/xt_state.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_owner.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>

void statistics_monitor_apps_rtt_via_uid(int if_index, int rtt, struct sock *sk);

#endif /* _OPPO_APPS_MONITOR_H */

