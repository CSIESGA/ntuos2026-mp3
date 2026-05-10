// #include "types.h"
// #include "param.h"
// #include "memlayout.h"
// #include "riscv.h"
// #include "spinlock.h"
// #include "proc.h"
// #include "defs.h"

// extern struct proc proc[];

// // ─────────────────────────────────────────────────────────────
// // Scheduling algorithms
// //
// // Each algorithm implements:
// //   struct sched_result schedule_<algo>(void)
// //
// // Returns the best RUNNABLE RT process and its computed
// // allocated_time.  If no RT task is runnable, .process is 0.
// // If a deadline miss is detected (RUNNABLE task with ticks > deadline),
// // returns that task with allocated_time = 0 (miss-in-dispatch signal).
// // The caller handles locking, dispatch logging, and swtch.
// // ─────────────────────────────────────────────────────────────

// struct sched_result
// schedule_default(void)
// {
//   struct sched_result result = {0, 0};

//   for(struct proc *p = proc; p < &proc[NPROC]; p++){
//     if(p->state == RUNNABLE && p->is_real_time){
//       result.process = p;
//       if(p->current_exec_ticks < p->exec_time &&
//          (int)ticks >= p->next_deadline){
//         result.allocated_time = 0;
//       }else{
//         result.allocated_time = p->exec_time - p->current_exec_ticks;
//       }
//       return result;
//     }
//   }

//   return result;
// }

// struct sched_result
// schedule_efdf(void)
// {
//   // TODO, mp3 part 2, EFDF
//   return schedule_default();
// }

// struct sched_result
// schedule_priority(void)
// {
//   // TODO, mp3 part 2, priority
//   return schedule_default();
// }

// void on_cycle_ended(struct proc *p, int deadline_miss)
// {
//   // TODO, mp3 part 2, DBP
//   // Update DBP history and distance with deadline outcome
// }

// struct sched_result
// schedule_dbp(void)
// {
//   // TODO, mp3 part 2, DBP
//   return schedule_default();
// }
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern struct proc proc[];

static int
min_int(int a, int b)
{
  return a < b ? a : b;
}

static int
has_missed_deadline(struct proc *p, int now)
{
  return p->current_exec_ticks < p->exec_time && now >= p->next_deadline;
}

static int
remaining_exec_time(struct proc *p)
{
  return p->exec_time - p->current_exec_ticks;
}

static int
remaining_deadline_time(struct proc *p, int now)
{
  return p->next_deadline - now;
}

static int
future_release_delta(struct proc *p, int now)
{
  return p->next_period_start - now;
}

static int
is_sleeping_for_future_release(struct proc *p, int now)
{
  return p->is_real_time && p->waiting_for_period && future_release_delta(p, now) > 0;
}

static int
priority_cmp(struct proc *a, struct proc *b)
{
  if(a->priority != b->priority)
    return a->priority - b->priority;
  return a->pid - b->pid;
}

static int
deadline_cmp(struct proc *a, struct proc *b)
{
  if(a->next_deadline != b->next_deadline)
    return a->next_deadline - b->next_deadline;
  return a->pid - b->pid;
}

static int
efdf_laxity_at(struct proc *p, int when)
{
  return p->next_deadline - when - remaining_exec_time(p);
}

static int
efdf_feasible_at(struct proc *p, int when)
{
  return efdf_laxity_at(p, when) >= 0;
}

static int
dbp_cmp(struct proc *a, struct proc *b)
{
  if(a->current_distance != b->current_distance)
    return a->current_distance - b->current_distance;
  if(a->next_deadline != b->next_deadline)
    return a->next_deadline - b->next_deadline;
  return a->pid - b->pid;
}

struct sched_result
schedule_default(void)
{
  struct sched_result result = {0, 0};

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(p->state == RUNNABLE && p->is_real_time){
      result.process = p;
      if(p->current_exec_ticks < p->exec_time &&
         (int)ticks >= p->next_deadline){
        result.allocated_time = 0;
      }else{
        result.allocated_time = p->exec_time - p->current_exec_ticks;
      }
      return result;
    }
  }

  return result;
}

struct sched_result
schedule_efdf(void)
{
  int now = (int)ticks;
  struct proc *missed = 0;
  struct proc *infeasible = 0;
  struct proc *best = 0;

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(p->state != RUNNABLE || !p->is_real_time)
      continue;

    if(has_missed_deadline(p, now)){
      if(missed == 0 || p->pid < missed->pid)
        missed = p;
    }
  }

  if(missed)
    return (struct sched_result){missed, 0};

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(p->state != RUNNABLE || !p->is_real_time)
      continue;

    if(!efdf_feasible_at(p, now)){
      if(infeasible == 0 || p->pid < infeasible->pid)
        infeasible = p;
    }
  }

  if(infeasible)
    return (struct sched_result){infeasible, 0};

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(p->state != RUNNABLE || !p->is_real_time)
      continue;

    if(best == 0 || deadline_cmp(p, best) < 0)
      best = p;
  }

  if(best == 0)
    return (struct sched_result){0, 0};

  int allocated = remaining_exec_time(best);
  allocated = min_int(allocated, remaining_deadline_time(best, now));

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(!is_sleeping_for_future_release(p, now))
      continue;

    int wake_at = p->next_period_start;
    if(efdf_feasible_at(p, wake_at) && deadline_cmp(p, best) < 0)
      allocated = min_int(allocated, future_release_delta(p, now));
  }

  return (struct sched_result){best, allocated};
}

struct sched_result
schedule_priority(void)
{
  int now = (int)ticks;
  struct proc *missed = 0;
  struct proc *best = 0;

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(p->state != RUNNABLE || !p->is_real_time)
      continue;

    if(has_missed_deadline(p, now)){
      if(missed == 0 || p->pid < missed->pid)
        missed = p;
      continue;
    }

    if(best == 0 || priority_cmp(p, best) < 0)
      best = p;
  }

  if(missed)
    return (struct sched_result){missed, 0};

  if(best == 0)
    return (struct sched_result){0, 0};

  int allocated = remaining_exec_time(best);
  allocated = min_int(allocated, remaining_deadline_time(best, now));

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(is_sleeping_for_future_release(p, now) && priority_cmp(p, best) < 0)
      allocated = min_int(allocated, future_release_delta(p, now));
  }

  return (struct sched_result){best, allocated};
}

void on_cycle_ended(struct proc *p, int deadline_miss)
{
  int k;
  int m;
  int successes = 0;

  if(p == 0)
    return;

  k = p->firm_k;
  m = p->firm_m;
  if(k <= 0)
    return;
  if(k > 8)
    k = 8;

  for(int i = k - 1; i > 0; i--)
    p->history[i] = p->history[i - 1];
  p->history[0] = deadline_miss ? 0 : 1;

  p->current_distance = 0;
  for(int i = 0; i < k; i++){
    if(p->history[i]){
      successes++;
      if(successes == m){
        p->current_distance = k - i;
        return;
      }
    }
  }
}

struct sched_result
schedule_dbp(void)
{
  int now = (int)ticks;
  struct proc *missed = 0;
  struct proc *best = 0;

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(p->state != RUNNABLE || !p->is_real_time)
      continue;

    if(has_missed_deadline(p, now)){
      if(missed == 0 || p->pid < missed->pid)
        missed = p;
      continue;
    }

    if(best == 0 || dbp_cmp(p, best) < 0)
      best = p;
  }

  if(missed)
    return (struct sched_result){missed, 0};

  if(best == 0)
    return (struct sched_result){0, 0};

  int allocated = remaining_exec_time(best);
  allocated = min_int(allocated, remaining_deadline_time(best, now));

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(is_sleeping_for_future_release(p, now) && dbp_cmp(p, best) < 0)
      allocated = min_int(allocated, future_release_delta(p, now));
  }

  return (struct sched_result){best, allocated};
}
