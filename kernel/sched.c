#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern struct proc proc[];

// ─────────────────────────────────────────────────────────────
// Scheduling algorithms
//
// Each algorithm implements:
//   struct sched_result schedule_<algo>(void)
//
// Returns the best RUNNABLE RT process and its computed
// allocated_time.  If no RT task is runnable, .process is 0.
// If a deadline miss is detected (RUNNABLE task with ticks > deadline),
// returns that task with allocated_time = 0 (miss-in-dispatch signal).
// The caller handles locking, dispatch logging, and swtch.
// ─────────────────────────────────────────────────────────────

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
  // TODO, mp3 part 2, EFDF
  return schedule_default();
}

struct sched_result
schedule_priority(void)
{
  // TODO, mp3 part 2, priority
  return schedule_default();
}

void on_cycle_ended(struct proc *p, int deadline_miss)
{
  // TODO, mp3 part 2, DBP
  // Update DBP history and distance with deadline outcome
}

struct sched_result
schedule_dbp(void)
{
  // TODO, mp3 part 2, DBP
  return schedule_default();
}
