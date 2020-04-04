/**********************************************************************************
* Copyright (c), 2008-2019 , Guangdong OPPO Mobile Comm Corp., Ltd.
* VENDOR_EDIT
* File: oppo_cfs_futex.c
* Description: UI First
* Version: 2.0
* Date: 2019-10-01
* Author: Liujie.Xie@TECH.BSP.Kernel.Sched
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0        2019-05-22       Liujie.Xie@TECH.BSP.Kernel.Sched      Created for UI First
* Revision 2.0        2019-10-01       Liujie.Xie@TECH.BSP.Kernel.Sched      Add for UI First 2.0
***********************************************************************************/

#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/pid.h>
#include <linux/oppocfs/oppo_cfs_common.h>

struct task_struct *get_futex_owner(u32 __user *uaddr2) {
    int owner_tid = -1;
    struct task_struct *futex_owner = NULL;

    if (uaddr2 != NULL) {
        if (copy_from_user(&owner_tid, uaddr2, sizeof(int))) {
            ux_warn("failed to get tid from uaddr2(curr:%-12s pid:%d)\n", current->comm, owner_tid);
        } else if (owner_tid != 0) {
            rcu_read_lock();
            futex_owner = find_task_by_vpid(owner_tid);
            rcu_read_unlock();
            if (futex_owner == NULL) {
                ux_warn("failed to find task by pid(curr:%-12s pid:%d)\n", current->comm, owner_tid);
            }
        }
    }

    return futex_owner;
}

void futex_dynamic_ux_enqueue(struct task_struct *owner, struct task_struct *task)
{
    bool is_ux = false;
    is_ux = test_set_dynamic_ux(task);

    if (is_ux && owner && !test_task_ux(owner)) {
        dynamic_ux_enqueue(owner, DYNAMIC_UX_FUTEX, task->ux_depth);
    }
}

void futex_dynamic_ux_dequeue(struct task_struct *task)
{

    if (test_dynamic_ux(task, DYNAMIC_UX_FUTEX)) {
        dynamic_ux_dequeue(task, DYNAMIC_UX_FUTEX);
    }
}
