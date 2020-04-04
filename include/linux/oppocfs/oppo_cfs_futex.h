/**********************************************************************************
* Copyright (c), 2008-2019 , Guangdong OPPO Mobile Comm Corp., Ltd.
* VENDOR_EDIT
* File: oppo_cfs_futex.h
* Description: UI First
* Version: 2.0
* Date: 2019-10-01
* Author: Liujie.Xie@TECH.BSP.Kernel.Sched
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0        2019-05-22       Liujie.Xie@TECH.BSP.Kernel.Sched      Created for UI First
* Revision 2.0        2019-10-01       Liujie.Xie@TECH.BSP.Kernel.Sched      Add for UI First 2.0
***********************************************************************************/

#ifndef _OPPO_CFS_FUTEX_H_
#define _OPPO_CFS_FUTEX_H_
extern bool test_task_ux(struct task_struct *task);
extern struct task_struct *get_futex_owner(u32 __user *uaddr2);
extern void futex_dynamic_ux_enqueue(struct task_struct *owner, struct task_struct *task);
extern void futex_dynamic_ux_dequeue(struct task_struct *task);
#endif
