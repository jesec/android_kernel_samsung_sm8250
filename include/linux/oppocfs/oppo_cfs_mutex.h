/**********************************************************************************
* Copyright (c), 2008-2019 , Guangdong OPPO Mobile Comm Corp., Ltd.
* VENDOR_EDIT
* File: oppo_cfs_mutex.h
* Description: UI First
* Version: 2.0
* Date: 2019-10-01
* Author: Liujie.Xie@TECH.BSP.Kernel.Sched
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0        2019-05-22       Liujie.Xie@TECH.BSP.Kernel.Sched      Created for UI First
* Revision 2.0        2019-10-01       Liujie.Xie@TECH.BSP.Kernel.Sched      Add for UI First 2.0
***********************************************************************************/

#ifndef _OPPO_CFS_MUTEX_H_
#define _OPPO_CFS_MUTEX_H_
extern void mutex_list_add(struct task_struct *task, struct list_head *entry, struct list_head *head, struct mutex *lock);
extern void mutex_dynamic_ux_enqueue(struct mutex *lock, struct task_struct *task);
extern void mutex_dynamic_ux_dequeue(struct mutex *lock, struct task_struct *task);
#endif
