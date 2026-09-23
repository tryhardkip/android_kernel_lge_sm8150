/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BORE (Burst-Oriented Response Enhancer) scheduler — 6.8.0 backport.
 *
 * This header provides inline helpers and extern declarations for the
 * BORE scheduler logic that is shared between kernel/sched/bore.c and
 * the other scheduler files (fair.c, core.c, debug.c).
 *
 * Adaptation notes for the 4.14 kernel:
 *  - Rather than introducing a separate struct bore_ctx in task_struct
 *    (as done in the 6.8.0 patch for 6.12), we keep the existing inline
 *    burst fields in struct sched_entity.  The helpers below access
 *    those fields indirectly via the sched_entity to remain compatible.
 *  - Static keys are used to gate expensive code paths.
 *  - The futex-waiting flag is stored as a standalone field in sched_entity
 *    (bore_futex_waiting) added under CONFIG_SCHED_BORE.
 */
#ifndef _KERNEL_SCHED_BORE_H
#define _KERNEL_SCHED_BORE_H

#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/ktime.h>
#include <asm/div64.h>

#define SCHED_BORE_AUTHOR	"Masahito Suzuki"
#define SCHED_BORE_PROGNAME	"BORE CPU Scheduler modification"
#define SCHED_BORE_VERSION	"6.8.0"

/* tunables */
extern u8   __read_mostly sched_bore;
extern u8   __read_mostly sched_burst_inherit_type;
extern u8   __read_mostly sched_burst_smoothness;
extern u8   __read_mostly sched_burst_penalty_offset;
extern uint __read_mostly sched_burst_penalty_scale;
extern uint __read_mostly sched_burst_cache_lifetime;
extern u8   __read_mostly sched_burst_protect_slice_lv;

DECLARE_STATIC_KEY_TRUE(sched_bore_key);
DECLARE_STATIC_KEY_TRUE(sched_burst_inherit_key);
DECLARE_STATIC_KEY_TRUE(sched_burst_ancestor_key);
DECLARE_STATIC_KEY_TRUE(sched_burst_protect_slice_cond_key);
DECLARE_STATIC_KEY_FALSE(sched_burst_protect_slice_prefer_key);

/* Inline helper: score in [0,15] range */
static inline u8 bore_score(struct task_struct *p)
{
	return p->se.burst_penalty >> 8;
}

/* Exported functions in kernel/sched/bore.c */
extern u8   effective_prio_bore(struct task_struct *p);
extern void update_curr_bore(struct task_struct *p, u64 delta_exec);
extern void restart_burst_bore(struct task_struct *p);
extern void restart_burst_rescale_deadline_bore(struct task_struct *p);
extern void task_fork_bore(struct task_struct *p, struct task_struct *parent,
			   unsigned long clone_flags, u64 now);
extern void sched_init_bore(void);
extern void reset_task_bore(struct task_struct *p);
extern void sched_bore_update_key(void);
extern void sched_bore_update_inherit_type(void);
extern void sched_bore_update_protect_slice_lv(void);

extern void reweight_entity(
	struct cfs_rq *cfs_rq, struct sched_entity *se, unsigned long weight);

#endif /* _KERNEL_SCHED_BORE_H */
