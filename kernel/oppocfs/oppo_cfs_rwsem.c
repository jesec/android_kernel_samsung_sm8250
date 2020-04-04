/**********************************************************************************
* Copyright (c), 2008-2019 , Guangdong OPPO Mobile Comm Corp., Ltd.
* VENDOR_EDIT
* File: oppo_cfs_rwsem.c
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
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/oppocfs/oppo_cfs_common.h>

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
};

#define RWSEM_READER_OWNED	((struct task_struct *)1UL)

static inline bool rwsem_owner_is_writer(struct task_struct *owner)
{
	return owner && owner != RWSEM_READER_OWNED;
}

static void rwsem_list_add_ux(struct list_head *entry, struct list_head *head)
{
    struct list_head *pos = NULL;
    struct list_head *n = NULL;
    struct rwsem_waiter *waiter = NULL;
    list_for_each_safe(pos, n, head) {
        waiter = list_entry(pos, struct rwsem_waiter, list);
        if (!test_task_ux(waiter->task) && waiter->task->prio > MAX_RT_PRIO) {
            list_add(entry, waiter->list.prev);
            return;
        }
    }
    if (pos == head) {
        list_add_tail(entry, head);
    }
}

bool rwsem_list_add(struct task_struct *tsk, struct list_head *entry, struct list_head *head, struct rw_semaphore *sem)
{
	bool is_ux = test_task_ux(tsk);
	if (!entry || !head) {
		return false;
	}
	if (is_ux) {
		rwsem_list_add_ux(entry, head);
        sem->m_count++;
        return true;
	} else {
		//list_add_tail(entry, head);
		return false;
	}

    return false;
}

void rwsem_dynamic_ux_enqueue(struct task_struct *tsk, struct task_struct *waiter_task, struct task_struct *owner, struct rw_semaphore *sem)
{
	bool is_ux = test_set_dynamic_ux(tsk);
	if (waiter_task && is_ux) {
		if (rwsem_owner_is_writer(owner) && !test_task_ux(owner) && sem && !sem->ux_dep_task) {
			dynamic_ux_enqueue(owner, DYNAMIC_UX_RWSEM, tsk->ux_depth);
			sem->ux_dep_task = owner;
		}
	}
}

void rwsem_dynamic_ux_dequeue(struct rw_semaphore *sem, struct task_struct *tsk)
{
	if (tsk && sem && sem->ux_dep_task == tsk) {
		dynamic_ux_dequeue(tsk, DYNAMIC_UX_RWSEM);
		sem->ux_dep_task = NULL;
	}
}
