/**********************************************************************************
* Copyright (c), 2008-2019 , Guangdong OPPO Mobile Comm Corp., Ltd.
* VENDOR_EDIT
* File: oppo_cfs_rwsem.h
* Description: UI First
* Version: 2.0
* Date: 2019-10-01
* Author: Liujie.Xie@TECH.BSP.Kernel.Sched
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0        2019-05-22       Liujie.Xie@TECH.BSP.Kernel.Sched      Created for UI First
* Revision 2.0        2019-10-01       Liujie.Xie@TECH.BSP.Kernel.Sched      Add for UI First 2.0
***********************************************************************************/

#ifndef _OPPO_CFS_RWSEM_H_
#define _OPPO_CFS_RWSEM_H_
extern bool rwsem_list_add(struct task_struct *tsk, struct list_head *entry, struct list_head *head, struct rw_semaphore *sem);
extern void rwsem_dynamic_ux_enqueue(struct task_struct *tsk, struct task_struct *waiter_task, struct task_struct *owner, struct rw_semaphore *sem);
extern void rwsem_dynamic_ux_dequeue(struct rw_semaphore *sem, struct task_struct *tsk);
#endif
