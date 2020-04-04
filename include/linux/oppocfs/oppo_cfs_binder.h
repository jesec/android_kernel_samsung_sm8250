/**********************************************************************************
* Copyright (c), 2008-2019 , Guangdong OPPO Mobile Comm Corp., Ltd.
* VENDOR_EDIT
* File: oppo_cfs_binder.h
* Description: UI First
* Version: 2.0
* Date: 2019-10-01
* Author: Liujie.Xie@TECH.BSP.Kernel.Sched
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0        2019-05-22       Liujie.Xie@TECH.BSP.Kernel.Sched      Created for UI First
* Revision 2.0        2019-10-01       Liujie.Xie@TECH.BSP.Kernel.Sched      Add for UI First 2.0
***********************************************************************************/

#ifndef _OPPO_CFS_BINDER_H_
#define _OPPO_CFS_BINDER_H_
#include <linux/oppocfs/oppo_cfs_common.h>
static inline void binder_thread_check_and_set_dynamic_ux(struct task_struct *thread_task, struct task_struct *from_task)
{
    if (from_task && test_set_dynamic_ux(from_task) && !test_task_ux(thread_task)) {
        dynamic_ux_enqueue(thread_task, DYNAMIC_UX_BINDER, from_task->ux_depth);
    }
}

static inline void binder_thread_check_and_remove_dynamic_ux(struct task_struct *thread_task)
{
    if (test_dynamic_ux(thread_task, DYNAMIC_UX_BINDER)) {
        dynamic_ux_dequeue(thread_task, DYNAMIC_UX_BINDER);
    }
}
#endif
