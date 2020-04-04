#include <linux/cgroup.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>

#include <trace/events/sched.h>
#ifdef VENDOR_EDIT
/* huangzhigen@oppo.com, 2019/05/09 add for frame boost 2.0 */
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>
#endif

#include "sched.h"

bool schedtune_initialized = false;
extern struct reciprocal_value schedtune_spc_rdiv;

/* We hold schedtune boost in effect for at least this long */
#define SCHEDTUNE_BOOST_HOLD_NS 50000000ULL

/*
 * EAS scheduler tunables for task groups.
 *
 * When CGroup support is enabled, we have to synchronize two different
 * paths:
 *  - slow path: where CGroups are created/updated/removed
 *  - fast path: where tasks in a CGroups are accounted
 *
 * The slow path tracks (a limited number of) CGroups and maps each on a
 * "boost_group" index. The fastpath accounts tasks currently RUNNABLE on each
 * "boost_group".
 *
 * Once a new CGroup is created, a boost group idx is assigned and the
 * corresponding "boost_group" marked as valid on each CPU.
 * Once a CGroup is release, the corresponding "boost_group" is marked as
 * invalid on each CPU. The CPU boost value (boost_max) is aggregated by
 * considering only valid boost_groups with a non null tasks counter.
 *
 * .:: Locking strategy
 *
 * The fast path uses a spin lock for each CPU boost_group which protects the
 * tasks counter.
 *
 * The "valid" and "boost" values of each CPU boost_group is instead
 * protected by the RCU lock provided by the CGroups callbacks. Thus, only the
 * slow path can access and modify the boost_group attribtues of each CPU.
 * The fast path will catch up the most updated values at the next scheduling
 * event (i.e. enqueue/dequeue).
 *
 *                                                        |
 *                                             SLOW PATH  |   FAST PATH
 *                              CGroup add/update/remove  |   Scheduler enqueue/dequeue events
 *                                                        |
 *                                                        |
 *                                                        |     DEFINE_PER_CPU(struct boost_groups)
 *                                                        |     +--------------+----+---+----+----+
 *                                                        |     |  idle        |    |   |    |    |
 *                                                        |     |  boost_max   |    |   |    |    |
 *                                                        |  +---->lock        |    |   |    |    |
 *  struct schedtune                  allocated_groups    |  |  |  group[    ] |    |   |    |    |
 *  +------------------------------+         +-------+    |  |  +--+---------+-+----+---+----+----+
 *  | idx                          |         |       |    |  |     |  valid  |
 *  | boots / prefer_idle          |         |       |    |  |     |  boost  |
 *  | perf_{boost/constraints}_idx | <---------+(*)  |    |  |     |  tasks  | <------------+
 *  | css                          |         +-------+    |  |     +---------+              |
 *  +-+----------------------------+         |       |    |  |     |         |              |
 *    ^                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    | zmalloc                              |       |    |  |     |         |              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    +                              BOOSTGROUPS_COUNT    |  |     BOOSTGROUPS_COUNT        |
 *  schedtune_boostgroup_init()                           |  +                              |
 *                                                        |  schedtune_{en,de}queue_task()  |
 *                                                        |                                 +
 *                                                        |          schedtune_tasks_update()
 *                                                        |
 */

/* SchdTune tunables for a group of tasks */
struct schedtune {
	/* SchedTune CGroup subsystem */
	struct cgroup_subsys_state css;

	/* Boost group allocated ID */
	int idx;

	/* Boost value for tasks on that SchedTune CGroup */
	int boost;

#ifdef CONFIG_SCHED_WALT
	/* Toggle ability to override sched boost enabled */
	bool sched_boost_no_override;

	/*
	 * Controls whether a cgroup is eligible for sched boost or not. This
	 * can temporariliy be disabled by the kernel based on the no_override
	 * flag above.
	 */
	bool sched_boost_enabled;

	/*
	 * Controls whether tasks of this cgroup should be colocated with each
	 * other and tasks of other cgroups that have the same flag turned on.
	 */
	bool colocate;

	/* Controls whether further updates are allowed to the colocate flag */
	bool colocate_update_disabled;
#endif /* CONFIG_SCHED_WALT */

	/* Hint to bias scheduling of tasks on that SchedTune CGroup
	 * towards idle CPUs */
	int prefer_idle;

#ifdef VENDOR_EDIT
	s64 defered;
#endif
};

static inline struct schedtune *css_st(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct schedtune, css) : NULL;
}

static inline struct schedtune *task_schedtune(struct task_struct *tsk)
{
	return css_st(task_css(tsk, schedtune_cgrp_id));
}

static inline struct schedtune *parent_st(struct schedtune *st)
{
	return css_st(st->css.parent);
}

/*
 * SchedTune root control group
 * The root control group is used to defined a system-wide boosting tuning,
 * which is applied to all tasks in the system.
 * Task specific boost tuning could be specified by creating and
 * configuring a child control group under the root one.
 * By default, system-wide boosting is disabled, i.e. no boosting is applied
 * to tasks which are not into a child control group.
 */
static struct schedtune
root_schedtune = {
	.boost	= 0,
#ifdef CONFIG_SCHED_WALT
	.sched_boost_no_override = false,
	.sched_boost_enabled = true,
	.colocate = false,
	.colocate_update_disabled = false,
#endif
	.prefer_idle = 0,
#ifdef VENDOR_EDIT
	.defered = 0,
#endif
};

/*
 * Maximum number of boost groups to support
 * When per-task boosting is used we still allow only limited number of
 * boost groups for two main reasons:
 * 1. on a real system we usually have only few classes of workloads which
 *    make sense to boost with different values (e.g. background vs foreground
 *    tasks, interactive vs low-priority tasks)
 * 2. a limited number allows for a simpler and more memory/time efficient
 *    implementation especially for the computation of the per-CPU boost
 *    value
 */
#ifdef VENDOR_EDIT
#define BOOSTGROUPS_COUNT 8
#else
#define BOOSTGROUPS_COUNT 6
#endif

/* Array of configured boostgroups */
static struct schedtune *allocated_group[BOOSTGROUPS_COUNT] = {
	&root_schedtune,
	NULL,
};

/* SchedTune boost groups
 * Keep track of all the boost groups which impact on CPU, for example when a
 * CPU has two RUNNABLE tasks belonging to two different boost groups and thus
 * likely with different boost values.
 * Since on each system we expect only a limited number of boost groups, here
 * we use a simple array to keep track of the metrics required to compute the
 * maximum per-CPU boosting value.
 */

#ifdef VENDOR_EDIT
struct boost_group_timer {
	/* Boost defered time */
	s64 defered;
	/* Number of cpu */
	int cpu;
	/* Schedtune index */
	int index;
	/* Boost of defered */
	int dboost;
	/* Hrtimer */
	struct hrtimer timer;
};

/* Work/Worker to boost cpufreq */
struct boost_work {
	int cpu;
	struct kthread_work work;
	struct irq_work irq_work;
};

static struct boost_worker {
	struct kthread_worker worker;
	struct task_struct *thread;
} st_boost_worker;

DEFINE_PER_CPU(struct boost_work, cpu_boost_work);
#endif

struct boost_groups {
	/* Maximum boost value for all RUNNABLE tasks on a CPU */
	int boost_max;
	u64 boost_ts;
	struct {
		/* True when this boost group maps an actual cgroup */
		bool valid;
		/* The boost for tasks on that boost group */
		int boost;
		/* Count of RUNNABLE tasks on that boost group */
		unsigned tasks;
		/* Timestamp of boost activation */
		u64 ts;
	} group[BOOSTGROUPS_COUNT];
#ifdef VENDOR_EDIT
	/* Boost group timer */
	struct boost_group_timer bg_timer[BOOSTGROUPS_COUNT];
#endif
	/* CPU's boost group locking */
	raw_spinlock_t lock;
};

/* Boost groups affecting each CPU in the system */
DEFINE_PER_CPU(struct boost_groups, cpu_boost_groups);

#ifdef CONFIG_SCHED_WALT
static inline void init_sched_boost(struct schedtune *st)
{
	st->sched_boost_no_override = false;
	st->sched_boost_enabled = true;
	st->colocate = false;
	st->colocate_update_disabled = false;
}

bool same_schedtune(struct task_struct *tsk1, struct task_struct *tsk2)
{
	return task_schedtune(tsk1) == task_schedtune(tsk2);
}

void update_cgroup_boost_settings(void)
{
	int i;

	for (i = 0; i < BOOSTGROUPS_COUNT; i++) {
		if (!allocated_group[i])
			break;

		if (allocated_group[i]->sched_boost_no_override)
			continue;

		allocated_group[i]->sched_boost_enabled = false;
	}
}

void restore_cgroup_boost_settings(void)
{
	int i;

	for (i = 0; i < BOOSTGROUPS_COUNT; i++) {
		if (!allocated_group[i])
			break;

		allocated_group[i]->sched_boost_enabled = true;
	}
}

bool task_sched_boost(struct task_struct *p)
{
	struct schedtune *st;
	bool sched_boost_enabled;

	rcu_read_lock();
	st = task_schedtune(p);
	sched_boost_enabled = st->sched_boost_enabled;
	rcu_read_unlock();

	return sched_boost_enabled;
}

static u64
sched_boost_override_read(struct cgroup_subsys_state *css,
					struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->sched_boost_no_override;
}

static int sched_boost_override_write(struct cgroup_subsys_state *css,
				struct cftype *cft, u64 override)
{
	struct schedtune *st = css_st(css);

	st->sched_boost_no_override = !!override;

	return 0;
}

#endif /* CONFIG_SCHED_WALT */

static inline bool schedtune_boost_timeout(u64 now, u64 ts)
{
	return ((now - ts) > SCHEDTUNE_BOOST_HOLD_NS);
}

static inline bool
schedtune_boost_group_active(int idx, struct boost_groups* bg, u64 now)
{
	if (bg->group[idx].tasks)
		return true;

#ifdef VENDOR_EDIT
	if (bg->bg_timer[idx].defered)
		return false;
#endif

	return !schedtune_boost_timeout(now, bg->group[idx].ts);
}
#ifdef VENDOR_EDIT
/* Used for update defered(-1) and boost */
static inline void
schedtune_queue_boost_work(struct boost_groups* bg, int idx, int cpu)
{
	struct boost_work *bw;

	/* Instantly queue boost work to kthread if defered is negative */
	if (bg->bg_timer[idx].defered < 0 && bg->group[idx].tasks > 0) {
		bw = &per_cpu(cpu_boost_work, cpu);
		kthread_queue_work(&st_boost_worker.worker, &bw->work);
	}
}

static inline void
schedtune_update_cpufreq(int cpu) {
#ifdef CONFIG_SCHED_WALT
	cpufreq_update_util(cpu_rq(cpu), SCHED_CPUFREQ_WALT);
#else
	cpufreq_update_util(cpu_rq(cpu), 0);
#endif
}
#endif

static void
schedtune_cpu_update(int cpu, u64 now)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	int boost_max;
	u64 boost_ts;
	int idx;

	/* The root boost group is always active */
	boost_max = bg->group[0].boost;
	boost_ts = now;
	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx) {

		/* Ignore non boostgroups not mapping a cgroup */
		if (!bg->group[idx].valid)
			continue;

		/*
		 * A boost group affects a CPU only if it has
		 * RUNNABLE tasks on that CPU or it has hold
		 * in effect from a previous task.
		 */
		if (!schedtune_boost_group_active(idx, bg, now))
			continue;

#ifdef VENDOR_EDIT
		/* Defered does not affect to root schedtune */
		if (bg->bg_timer[idx].defered) {
			if (boost_max < bg->bg_timer[idx].dboost) {
				boost_max = bg->bg_timer[idx].dboost;
				boost_ts =  bg->group[idx].ts;
			}
			continue;
		}
#endif
		/* This boost group is active */
		if (boost_max > bg->group[idx].boost)
			continue;

		boost_max = bg->group[idx].boost;
		boost_ts =  bg->group[idx].ts;
	}

	/* Ensures boost_max is non-negative when all cgroup boost values
	 * are neagtive. Avoids under-accounting of cpu capacity which may cause
	 * task stacking and frequency spikes.*/
	boost_max = max(boost_max, 0);
	bg->boost_max = boost_max;
	bg->boost_ts = boost_ts;
}

static int
schedtune_boostgroup_update(int idx, int boost)
{
	struct boost_groups *bg;
	int cur_boost_max;
	int old_boost;
	int cpu;
	u64 now;

	/* Update per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);

		/* CGroups are never associated to non active cgroups */
		BUG_ON(!bg->group[idx].valid);

		/*
		 * Keep track of current boost values to compute the per CPU
		 * maximum only when it has been affected by the new value of
		 * the updated boost group
		 */
		cur_boost_max = bg->boost_max;
		old_boost = bg->group[idx].boost;

		/* Update the boost value of this boost group */
		bg->group[idx].boost = boost;

#ifdef VENDOR_EDIT
		/* Do not update boost max when defered was enabled.
		 * defered > 0 : update by hrtimer,
		 * defered < 0 : instantly update with queue work.
		 */
		if (bg->bg_timer[idx].defered < 0
				&& bg->bg_timer[idx].dboost != bg->group[idx].boost) {
			bg->bg_timer[idx].dboost = bg->group[idx].boost;
			schedtune_cpu_update(cpu, sched_clock_cpu(cpu));
			schedtune_queue_boost_work(bg, idx, cpu);
		}
		if (bg->bg_timer[idx].defered) {
			trace_sched_tune_boostgroup_update(cpu,
					bg->boost_max == cur_boost_max ?
					0 : (bg->boost_max < cur_boost_max ? -1 : 1),
					bg->boost_max);
			continue;
		}
#endif

		/* Check if this update increase current max */
		now = sched_clock_cpu(cpu);
		if (boost > cur_boost_max &&
			schedtune_boost_group_active(idx, bg, now)) {
			bg->boost_max = boost;
			bg->boost_ts = bg->group[idx].ts;

			trace_sched_tune_boostgroup_update(cpu, 1, bg->boost_max);
			continue;
		}

		/* Check if this update has decreased current max */
		if (cur_boost_max == old_boost && old_boost > boost) {
			schedtune_cpu_update(cpu, now);
			trace_sched_tune_boostgroup_update(cpu, -1, bg->boost_max);
			continue;
		}

		trace_sched_tune_boostgroup_update(cpu, 0, bg->boost_max);
	}

	return 0;
}

#define ENQUEUE_TASK  1
#define DEQUEUE_TASK -1

static inline bool
schedtune_update_timestamp(struct task_struct *p)
{
	if (sched_feat(SCHEDTUNE_BOOST_HOLD_ALL))
		return true;

	return task_has_rt_policy(p);
}

static inline void
schedtune_tasks_update(struct task_struct *p, int cpu, int idx, int task_count)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	int tasks = bg->group[idx].tasks + task_count;
#ifdef VENDOR_EDIT
	int boost_max;
	struct boost_work *bw;
#endif

	/* Update boosted tasks count while avoiding to make it negative */
	bg->group[idx].tasks = max(0, tasks);

	/* Update timeout on enqueue */
	if (task_count > 0) {
		u64 now = sched_clock_cpu(cpu);

		if (schedtune_update_timestamp(p))
			bg->group[idx].ts = now;

		/* Boost group activation or deactivation on that RQ */
		if (bg->group[idx].tasks == 1)
			schedtune_cpu_update(cpu, now);
	}

#ifdef VENDOR_EDIT
	/* Update boost max every dequeue/quque when defered was enabled and boost > 0 */
	if (!(bg->bg_timer[idx].defered && bg->group[idx].boost > 0))
		goto out;

	if (bg->group[idx].tasks == 0) {
		hrtimer_try_to_cancel(&bg->bg_timer[idx].timer);
		/* Cancel boosting */
		if (bg->bg_timer[idx].dboost > 0) {
			bg->bg_timer[idx].dboost = 0;
			schedtune_cpu_update(cpu, sched_clock_cpu(cpu));
			bw = &per_cpu(cpu_boost_work, cpu);
			irq_work_queue(&bw->irq_work);
		}
	} else if (bg->group[idx].tasks == 1
			|| (bg->bg_timer[idx].dboost > 0 &&
			bg->bg_timer[idx].dboost != bg->group[idx].boost)) {
		if (task_count != ENQUEUE_TASK)
			goto out;

		if (bg->bg_timer[idx].defered < 0) {
			bg->bg_timer[idx].dboost = bg->group[idx].boost;
			boost_max = bg->boost_max;
			schedtune_cpu_update(cpu, sched_clock_cpu(cpu));
			bw = &per_cpu(cpu_boost_work, cpu);
			if (boost_max != bg->boost_max)
				irq_work_queue(&bw->irq_work);
		} else if (!hrtimer_active(&bg->bg_timer[idx].timer)
				&& !hrtimer_callback_running(&bg->bg_timer[idx].timer))
			/* Setup a timer when enqueue a task of actived bg */
			hrtimer_start(&bg->bg_timer[idx].timer,
					ns_to_ktime(bg->bg_timer[idx].defered),
					HRTIMER_MODE_REL);
	}
out:
#endif

	trace_sched_tune_tasks_update(p, cpu, tasks, idx,
			bg->group[idx].boost, bg->boost_max,
			bg->group[idx].ts);
}

/*
 * NOTE: This function must be called while holding the lock on the CPU RQ
 */
void schedtune_enqueue_task(struct task_struct *p, int cpu)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	unsigned long irq_flags;
	struct schedtune *st;
	int idx;

	if (unlikely(!schedtune_initialized))
		return;

	/*
	 * Boost group accouting is protected by a per-cpu lock and requires
	 * interrupt to be disabled to avoid race conditions for example on
	 * do_exit()::cgroup_exit() and task migration.
	 */
	raw_spin_lock_irqsave(&bg->lock, irq_flags);
	rcu_read_lock();

	st = task_schedtune(p);
	idx = st->idx;

	schedtune_tasks_update(p, cpu, idx, ENQUEUE_TASK);

	rcu_read_unlock();
	raw_spin_unlock_irqrestore(&bg->lock, irq_flags);
}

int schedtune_can_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct cgroup_subsys_state *css;
	struct boost_groups *bg;
	struct rq_flags rq_flags;
	unsigned int cpu;
	struct rq *rq;
	int src_bg; /* Source boost group index */
	int dst_bg; /* Destination boost group index */
	int tasks;
	u64 now;

	if (unlikely(!schedtune_initialized))
		return 0;


	cgroup_taskset_for_each(task, css, tset) {

		/*
		 * Lock the CPU's RQ the task is enqueued to avoid race
		 * conditions with migration code while the task is being
		 * accounted
		 */
		rq = task_rq_lock(task, &rq_flags);

		if (!task->on_rq) {
			task_rq_unlock(rq, task, &rq_flags);
			continue;
		}

		/*
		 * Boost group accouting is protected by a per-cpu lock and requires
		 * interrupt to be disabled to avoid race conditions on...
		 */
		cpu = cpu_of(rq);
		bg = &per_cpu(cpu_boost_groups, cpu);
		raw_spin_lock(&bg->lock);

		dst_bg = css_st(css)->idx;
		src_bg = task_schedtune(task)->idx;

		/*
		 * Current task is not changing boostgroup, which can
		 * happen when the new hierarchy is in use.
		 */
		if (unlikely(dst_bg == src_bg)) {
			raw_spin_unlock(&bg->lock);
			task_rq_unlock(rq, task, &rq_flags);
			continue;
		}

		/*
		 * This is the case of a RUNNABLE task which is switching its
		 * current boost group.
		 */

		/* Move task from src to dst boost group */
		tasks = bg->group[src_bg].tasks - 1;
		bg->group[src_bg].tasks = max(0, tasks);
		bg->group[dst_bg].tasks += 1;

		/* Update boost hold start for this group */
		now = sched_clock_cpu(cpu);
		bg->group[dst_bg].ts = now;

		/* Force boost group re-evaluation at next boost check */
		bg->boost_ts = now - SCHEDTUNE_BOOST_HOLD_NS;

		raw_spin_unlock(&bg->lock);
		task_rq_unlock(rq, task, &rq_flags);
	}

	return 0;
}

#ifdef CONFIG_SCHED_WALT
static u64 sched_colocate_read(struct cgroup_subsys_state *css,
						struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->colocate;
}

static int sched_colocate_write(struct cgroup_subsys_state *css,
				struct cftype *cft, u64 colocate)
{
	struct schedtune *st = css_st(css);

	if (st->colocate_update_disabled)
		return -EPERM;

	st->colocate = !!colocate;
	st->colocate_update_disabled = true;
	return 0;
}

#else /* CONFIG_SCHED_WALT */

static inline void init_sched_boost(struct schedtune *st) { }

#endif /* CONFIG_SCHED_WALT */

void schedtune_cancel_attach(struct cgroup_taskset *tset)
{
	/* This can happen only if SchedTune controller is mounted with
	 * other hierarchies ane one of them fails. Since usually SchedTune is
	 * mouted on its own hierarcy, for the time being we do not implement
	 * a proper rollback mechanism */
	WARN(1, "SchedTune cancel attach not implemented");
}

/*
 * NOTE: This function must be called while holding the lock on the CPU RQ
 */
void schedtune_dequeue_task(struct task_struct *p, int cpu)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	unsigned long irq_flags;
	struct schedtune *st;
	int idx;

	if (unlikely(!schedtune_initialized))
		return;

	/*
	 * Boost group accouting is protected by a per-cpu lock and requires
	 * interrupt to be disabled to avoid race conditions on...
	 */
	raw_spin_lock_irqsave(&bg->lock, irq_flags);
	rcu_read_lock();

	st = task_schedtune(p);
	idx = st->idx;

	schedtune_tasks_update(p, cpu, idx, DEQUEUE_TASK);

	rcu_read_unlock();
	raw_spin_unlock_irqrestore(&bg->lock, irq_flags);
}

int schedtune_cpu_boost(int cpu)
{
	struct boost_groups *bg;
	u64 now;

	bg = &per_cpu(cpu_boost_groups, cpu);
	now = sched_clock_cpu(cpu);

	/* Check to see if we have a hold in effect */
	if (schedtune_boost_timeout(now, bg->boost_ts))
		schedtune_cpu_update(cpu, now);

	return bg->boost_max;
}

#ifdef VENDOR_EDIT
// Liujie.Xie@TECH.Kernel.Sched, 2019/05/22, add for ui first
extern bool test_task_ux(struct task_struct *task);
#endif
int schedtune_task_boost(struct task_struct *p)
{
	struct schedtune *st;
	int task_boost;
#ifdef VENDOR_EDIT
	struct boost_groups *bg;
#endif

	if (unlikely(!schedtune_initialized))
		return 0;

	/* Get task boost value */
	rcu_read_lock();
	st = task_schedtune(p);
	task_boost = st->boost;
#ifdef VENDOR_EDIT
// Liujie.Xie@TECH.Kernel.Sched, 2019/05/22, add for ui first
        if (sysctl_uifirst_enabled && sysctl_launcher_boost_enabled && test_task_ux(p)) {
                task_boost = 60;
        }
#endif
#ifdef VENDOR_EDIT
/* huangzhigen@oppo.com, 2019/05/09 add for frame boost 2.0 */
	bg = &per_cpu(cpu_boost_groups, task_cpu(p));
	if (bg->bg_timer[st->idx].defered)
		task_boost = bg->bg_timer[st->idx].dboost;
#endif
	rcu_read_unlock();

	return task_boost;
}

int schedtune_prefer_idle(struct task_struct *p)
{
	struct schedtune *st;
	int prefer_idle;

	if (unlikely(!schedtune_initialized))
		return 0;

	/* Get prefer_idle value */
	rcu_read_lock();
	st = task_schedtune(p);
	prefer_idle = st->prefer_idle;
#ifdef VENDOR_EDIT
// Liujie.Xie@TECH.Kernel.Sched, 2019/05/22, add for ui first
    if (sysctl_uifirst_enabled && sysctl_launcher_boost_enabled && test_task_ux(p)) {
        prefer_idle = 1;
    }
#endif
	rcu_read_unlock();

	return prefer_idle;
}

static u64
prefer_idle_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->prefer_idle;
}

static int
prefer_idle_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    u64 prefer_idle)
{
	struct schedtune *st = css_st(css);
	st->prefer_idle = !!prefer_idle;

	return 0;
}

static s64
boost_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->boost;
}

#ifdef CONFIG_SCHED_WALT
static void schedtune_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct cgroup_subsys_state *css;
	struct schedtune *st;
	bool colocate;

	cgroup_taskset_first(tset, &css);
	st = css_st(css);

	colocate = st->colocate;

	cgroup_taskset_for_each(task, css, tset)
		sync_cgroup_colocation(task, colocate);
}
#else
static void schedtune_attach(struct cgroup_taskset *tset)
{
}
#endif

static int
boost_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    s64 boost)
{
	struct schedtune *st = css_st(css);

	if (boost < 0 || boost > 100)
		return -EINVAL;

	st->boost = boost;

	/* Update CPU boost */
	schedtune_boostgroup_update(st->idx, st->boost);

	return 0;
}

#ifdef VENDOR_EDIT
#define DEFERED_MAX_MSEC	100
static int
schedtune_boostgroup_update_defered(int idx, s64 defered)
{
	struct boost_groups *bg;
	int cpu;
	int boost_max;

	/* Update per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);

		/* Update the defered value of this boost group */
		bg->bg_timer[idx].defered = defered * NSEC_PER_MSEC;

		/* Update the dboost value */
		if (defered == 0)
			bg->bg_timer[idx].dboost = 0;
		else if (defered < 0) {
			bg->bg_timer[idx].dboost = bg->group[idx].boost;
			/* Boost instantly if defered is negative */
			boost_max = bg->boost_max;
			schedtune_cpu_update(cpu, sched_clock_cpu(cpu));
			if (boost_max != bg->boost_max)
				schedtune_queue_boost_work(bg, idx, cpu);
		}
	}

	return 0;
}

static s64
defered_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct schedtune *st = css_st(css);
	return st->defered;
}

static int
defered_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    s64 defered)
{
	struct schedtune *st = css_st(css);

	if (defered < -1 || defered > DEFERED_MAX_MSEC)
		return -EINVAL;

	if (defered == st->defered)
		return 0;

	st->defered = defered;

	/* Update defered of boostgroup */
	schedtune_boostgroup_update_defered(st->idx, st->defered);

	return 0;
}
#endif

static struct cftype files[] = {
#ifdef CONFIG_SCHED_WALT
	{
		.name = "sched_boost_no_override",
		.read_u64 = sched_boost_override_read,
		.write_u64 = sched_boost_override_write,
	},
	{
		.name = "colocate",
		.read_u64 = sched_colocate_read,
		.write_u64 = sched_colocate_write,
	},
#endif
	{
		.name = "boost",
		.read_s64 = boost_read,
		.write_s64 = boost_write,
	},
	{
		.name = "prefer_idle",
		.read_u64 = prefer_idle_read,
		.write_u64 = prefer_idle_write,
	},
#ifdef VENDOR_EDIT
	{
		.name = "defered",
		.read_s64 = defered_read,
		.write_s64 = defered_write,
	},
#endif
	{ }	/* terminate */
};

#ifdef VENDOR_EDIT
static enum hrtimer_restart
schedtune_boostgroup_timer(struct hrtimer *htimer)
{
	unsigned long flags;
	struct boost_groups *bg;
	struct boost_work *bw;
	struct boost_group_timer *bg_timer =
			container_of(htimer, struct boost_group_timer, timer);
	int cpu = bg_timer->cpu;
	int update = 0;
	int boost_max;

	bg = &per_cpu(cpu_boost_groups, cpu);

	raw_spin_lock_irqsave(&bg->lock, flags);

	/* Update cpu boost value */
	if (bg_timer->defered > 0 && bg->group[bg_timer->index].tasks > 0) {
		bg_timer->dboost = bg->group[bg_timer->index].boost;
		boost_max = bg->boost_max;
		schedtune_cpu_update(cpu, sched_clock_cpu(cpu));
		if (boost_max != bg->boost_max)
			update = 1;
	}

	raw_spin_unlock_irqrestore(&bg->lock, flags);

	if (update) {
		bw = &per_cpu(cpu_boost_work, cpu);
		kthread_queue_work(&st_boost_worker.worker, &bw->work);
	}

	return HRTIMER_NORESTART;
}

static void
schedtune_boost_work(struct kthread_work *work)
{
	unsigned long flags;
	struct boost_work *bw = container_of(work, struct boost_work, work);
	int cpu = bw->cpu;

	/* It's better to check bg tasks before update cpufreq... */
	raw_spin_lock_irqsave(&cpu_rq(cpu)->lock, flags);
	schedtune_update_cpufreq(cpu);
	raw_spin_unlock_irqrestore(&cpu_rq(cpu)->lock, flags);
}

static void
schedtune_irq_work(struct irq_work *work)
{
	struct boost_work *bw =
			container_of(work, struct boost_work, irq_work);

	kthread_queue_work(&st_boost_worker.worker, &bw->work);
}

static inline int
schedtune_init_boost_kthread()
{
	int cpu;
	int ret;
	struct task_struct *thread;
	struct boost_work *bw;
	struct sched_param param = { .sched_priority = 1 };

	for_each_possible_cpu(cpu) {
		bw = &per_cpu(cpu_boost_work, cpu);
		bw->cpu = cpu;
		kthread_init_work(&bw->work, schedtune_boost_work);
		init_irq_work(&bw->irq_work, schedtune_irq_work);
	}
	st_boost_worker.thread = NULL;
	kthread_init_worker(&st_boost_worker.worker);
	thread = kthread_create(kthread_worker_fn,
			&st_boost_worker.worker, "kworker/st10:0");
	if (IS_ERR_OR_NULL(thread)) {
		pr_err("failed to create stbw thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setscheduler_nocheck(thread, SCHED_FIFO, &param);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_FIFO\n", __func__);
		return ret;
	}

	st_boost_worker.thread = thread;
	wake_up_process(thread);

	return 0;
}
#endif
static void
schedtune_boostgroup_init(struct schedtune *st, int idx)
{
	struct boost_groups *bg;
	int cpu;

	/* Initialize per CPUs boost group support */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		bg->group[idx].boost = 0;
		bg->group[idx].valid = true;
		bg->group[idx].ts = 0;
#ifdef VENDOR_EDIT
		bg->bg_timer[idx].defered = 0;
		bg->bg_timer[idx].dboost = 0;
		bg->bg_timer[idx].index = idx;
		bg->bg_timer[idx].cpu = cpu;
		hrtimer_init(&bg->bg_timer[idx].timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		bg->bg_timer[idx].timer.function = schedtune_boostgroup_timer;
#endif
	}

	/* Keep track of allocated boost groups */
	allocated_group[idx] = st;
	st->idx = idx;
}

static struct cgroup_subsys_state *
schedtune_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct schedtune *st;
	int idx;

	if (!parent_css)
		return &root_schedtune.css;

	/* Allow only single level hierachies */
	if (parent_css != &root_schedtune.css) {
		pr_err("Nested SchedTune boosting groups not allowed\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Allow only a limited number of boosting groups */
	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx)
		if (!allocated_group[idx])
			break;
	if (idx == BOOSTGROUPS_COUNT) {
		pr_err("Trying to create more than %d SchedTune boosting groups\n",
		       BOOSTGROUPS_COUNT);
		return ERR_PTR(-ENOSPC);
	}

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto out;

	/* Initialize per CPUs boost group support */
	init_sched_boost(st);
	schedtune_boostgroup_init(st, idx);

	return &st->css;

out:
	return ERR_PTR(-ENOMEM);
}

static void
schedtune_boostgroup_release(struct schedtune *st)
{
	struct boost_groups *bg;
	int cpu;
	
#ifdef VENDOR_EDIT
	/* Reset defered and dboost */
	schedtune_boostgroup_update_defered(st->idx, 0);
#endif

	/* Reset per CPUs boost group support */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		bg->group[st->idx].valid = false;
		bg->group[st->idx].boost = 0;
	}

	/* Keep track of allocated boost groups */
	allocated_group[st->idx] = NULL;
}

static void
schedtune_css_free(struct cgroup_subsys_state *css)
{
	struct schedtune *st = css_st(css);

	/* Release per CPUs boost group support */
	schedtune_boostgroup_release(st);
	kfree(st);
}

struct cgroup_subsys schedtune_cgrp_subsys = {
	.css_alloc	= schedtune_css_alloc,
	.css_free	= schedtune_css_free,
	.attach		= schedtune_attach,
	.can_attach     = schedtune_can_attach,
	.cancel_attach  = schedtune_cancel_attach,
	.legacy_cftypes	= files,
	.early_init	= 1,
};

static inline void
schedtune_init_cgroups(void)
{
	struct boost_groups *bg;
	int cpu;

	/* Initialize the per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		memset(bg, 0, sizeof(struct boost_groups));
		bg->group[0].valid = true;
		raw_spin_lock_init(&bg->lock);
#ifdef VENDOR_EDIT
		bg->bg_timer[0].cpu = cpu;
		hrtimer_init(&bg->bg_timer[0].timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		bg->bg_timer[0].timer.function = schedtune_boostgroup_timer;
#endif
	}

	pr_info("schedtune: configured to support %d boost groups\n",
		BOOSTGROUPS_COUNT);

	schedtune_initialized = true;
}

/*
 * Initialize the cgroup structures
 */
static int
schedtune_init(void)
{
	schedtune_spc_rdiv = reciprocal_value(100);
	schedtune_init_cgroups();
#ifdef VENDOR_EDIT
	schedtune_init_boost_kthread();
#endif
	return 0;
}
postcore_initcall(schedtune_init);
