#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_set_sched_algo(void)
{
  int algo;
  argint(0, &algo);
  if(algo < 0 || algo > 3)
    return -1;
  sched_algo = algo;
  sync_task_count = 0;
  sync_released = 0;
  return 0;
}

uint64
sys_start_periodic_task(void)
{
  int exec_time, period, num_cycles, priority, arrival_time;
  argint(0, &exec_time);
  argint(1, &period);
  argint(2, &num_cycles);
  argint(3, &priority);
  argint(4, &arrival_time);

  struct proc *p = myproc();

  // Register RT parameters
  p->is_real_time = 1;
  p->exec_time = exec_time;
  p->period = period;
  p->remaining_cycles = num_cycles;
  p->priority = priority;
  p->arrival_time = arrival_time;

  // Pre-set RT scheduling fields BEFORE sync sleep.
  // release_sync will reset ticks to 0, so these are relative to tick 0.
  // This ensures the scheduler sees correct deadlines when tasks first wake.
  p->next_deadline = arrival_time + period;
  p->next_period_start = arrival_time;
  p->current_exec_ticks = 0;

  // Wait for sync release (acts as the "starting gun")
  acquire(&sync_lock);
  sync_task_count++;
  wakeup(&sync_task_count);  // wake parent if waiting in release_sync
  if(arrival_time > 0){
    // For tasks with a future arrival, skip the sync sleep entirely.
    // Go directly to arrival sleep so we are never RUNNABLE to the RT
    // scheduler before our arrival time.  Acquire p->lock while still
    // holding sync_lock (same order as release_sync → wakeup) to
    // guarantee no scheduling window.  wake_sleeping_rt_tasks() won't
    // fire until sync_released=1 (after ticks is reset to 0), and
    // won't wake us until ticks >= arrival_time.
    acquire(&p->lock);
    p->state = SLEEPING;
    p->waiting_for_period = 1;
    // printf("[Debug]setup process#%d: arrival_sleep until %d\n", p->pid, arrival_time);
    release(&sync_lock);
    sched();
    release(&p->lock);
  } else {
    while(!sync_released){
      sleep(&sync_released, &sync_lock);
    }
    // printf("[Debug]setup process#%d: ready\n", p->pid);
    release(&sync_lock);
  }

  // Syscall returns to user space. The user program spins in a loop;
  // timer interrupts (usertrap) handle tick accounting via rt_yield().
  // When all cycles finish, rt_yield() marks the process as killed,
  // and usertrap() exits it via kexit().
  return 0;
}

uint64
sys_release_sync(void)
{
  int num_tasks;
  argint(0, &num_tasks);

  // Wait until all tasks have registered
  acquire(&sync_lock);
  while(sync_task_count < num_tasks){
    sleep(&sync_task_count, &sync_lock);
  }
  release(&sync_lock);

  // Signal the scheduler to reset ticks to 0 on the next dispatch.
  // We defer the reset so it happens with interrupts disabled inside
  // the scheduler loop — no timer can slip in between the reset and
  // the first dispatch.
  needs_ticks_reset = 1;

  // Wake all waiting tasks
  acquire(&sync_lock);
  sync_released = 1;
  wakeup(&sync_released);
  release(&sync_lock);

  return 0;
}

uint64
sys_set_firm_constraints(void)
{
  int pid, m, k;

  argint(0, &pid);
  argint(1, &m);
  argint(2, &k);

  // Validate constraints: m and k must be positive, m <= k
  if(m <= 0 || k <= 0 || m > k){
    printf("ERROR: set_firm_constraints: invalid values, m=%d, k=%d\n", m, k);
    return -1;
  }

  // Find process by pid
  struct proc *p = getproc(pid);
  if(!p)
    return -1;

  // Update constraints
  p->firm_m = m;
  p->firm_k = k;
  // Reset history to clean slate
  memset(p->history, 1, sizeof(p->history));
  p->current_distance = p->firm_k - p->firm_m + 1;

  release(&p->lock);
  return 0;
}
