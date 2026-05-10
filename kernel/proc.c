#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int proc_size = sizeof(struct proc);

int nextpid = 1;
struct spinlock pid_lock;

// Real-time scheduling global state
int sched_algo = 0;            // 0=RR, 1=Priority, 2=EDF
int sync_task_count = 0;       // Tasks waiting for sync release
int sync_released = 0;         // Set to 1 by release_sync
struct spinlock sync_lock;     // Protects sync state
int needs_ticks_reset = 0;    // Set by release_sync, cleared by scheduler

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  initlock(&sync_lock, "sync");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

struct proc*
getproc(int pid)
{
  push_off();
  // Find process by pid
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      break;
    }
    release(&p->lock);
  }
  pop_off();
  return p;
}

inline int
getprocindex(struct proc *p)
{
  return (p - proc) % sizeof(struct proc);
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // Initialize DBP (m,k)-firm constraint fields with defaults
  p->firm_k = 3;
  p->firm_m = 2;
  memset(p->history, 1, sizeof(p->history));  // Initialize history to all "met"
  p->current_distance = p->firm_k - p->firm_m + 1;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->is_real_time = 0;
  p->priority = 0;
  p->exec_time = 0;
  p->period = 0;
  p->remaining_cycles = 0;
  p->current_exec_ticks = 0;
  p->next_deadline = 0;
  p->next_period_start = 0;
  p->waiting_for_period = 0;
  p->arrival_time = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// ─────────────────────────────────────────────────────────────
// RT scheduling helpers
// ─────────────────────────────────────────────────────────────

// Wake sleeping RT tasks whose period has arrived.
// Called by scheduler() before each scheduling decision.
static void
wake_sleeping_rt_tasks(void)
{
  if(!sync_released)
    return;
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->is_real_time && p->waiting_for_period &&
       ticks >= (uint)p->next_period_start){
      // printf("[Debug]wake process#%d at tick %d, next_period_start=%d\n", p->pid, ticks, p->next_period_start);
      p->state = RUNNABLE;
      p->waiting_for_period = 0;
    }
    release(&p->lock);
  }
}

// Advance a process past its missed cycle.
// Called on deadline miss from both rt_yield ("in yield")
// and scheduler ("in scheduler").
// For "in yield": caller is the running process in kerneltrap context.
// For "in scheduler": caller is the scheduler (process is RUNNABLE, lock held).
static void
move_to_next_cycle_sleep(struct proc *p)
{
  p->remaining_cycles--;
  if(p->remaining_cycles > 0){
    p->next_period_start = p->next_deadline;
    p->next_deadline += p->period;
    p->current_exec_ticks = 0;
    // If next period hasn't arrived yet, sleep until it does
    if((int)ticks < p->next_period_start){
      p->state = SLEEPING;
      p->waiting_for_period = 1;
    }
  } else {
    p->is_real_time = 0;
    p->killed = 1;  // caller holds p->lock, so set directly
  }
}

// Called from kerneltrap when an RT process must yield
// (cycle complete or allocated_time expired).
// All scheduling logic lives here, not in trap.c.
void
rt_yield(struct proc *p)
{
  int cycle_done = (p->current_exec_ticks >= p->exec_time);
  int missed = ((int)ticks > p->next_deadline);

  if (cycle_done || missed) {
    on_cycle_ended(p, missed);
  }

  if(missed){
    // ── MISS IN YIELD ──
    printf("process#%d misses a deadline at %d in yield\n",
           p->pid, ticks);
    acquire(&p->lock);
    move_to_next_cycle_sleep(p);
    if(p->state == SLEEPING){
      sched();
      release(&p->lock);
    } else {
      // Remains RUNNABLE (next period already arrived) or done
      release(&p->lock);
      yield();
    }
    return;
  }

  if(cycle_done){
    // ── NORMAL FINISH ──
    p->remaining_cycles--;
    printf("process#%d finish one cycle at %d: %d cycles left\n",
           p->pid, ticks, p->remaining_cycles);
    if(p->remaining_cycles > 0){
      p->next_period_start = p->next_deadline;
      p->next_deadline += p->period;
      p->current_exec_ticks = 0;
      acquire(&p->lock);
      p->state = SLEEPING;
      p->waiting_for_period = 1;
      sched();
      release(&p->lock);
    } else {
      p->is_real_time = 0;
      setkilled(p);
      yield();
    }
    return;
  }

  // ── TIME SLICE EXPIRED ──
  yield();
}

// Dispatch a selected process: acquire its lock, set RUNNING, context switch,
// then release. For RT processes, also calls set_process_timer and logs dispatch.
static void
dispatch(struct cpu *c, struct sched_result sr)
{
  acquire(&sr.process->lock);
  if(sr.process->is_real_time){
    set_process_timer(sr.process, sr.allocated_time);
    printf("dispatch process#%d at %d: allocated_time=%d\n",
           sr.process->pid, ticks, sr.allocated_time);
  }
  sr.process->state = RUNNING;
  c->proc = sr.process;
  swtch(&c->context, &sr.process->context);
  c->proc = 0;
  release(&sr.process->lock);
}

static struct sched_result
schedule_rr(void)
{
  static int next_idx = 0;
  struct sched_result result = {0, 0};
  int i;

  for(i = 0; i < NPROC; i++){
    int idx = (next_idx + i) % NPROC;
    struct proc *p = &proc[idx];
    if(!p->is_real_time && p->state == RUNNABLE){
      result.process = p;
      result.allocated_time = 1;
      next_idx = (idx + 1) % NPROC;
      break;
    }
  }
  return result;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    if(sched_algo == SCHED_EFDF || sched_algo == SCHED_PRIORITY || sched_algo == SCHED_DBP){
      // Reset ticks to 0 on first iteration after sync release.
      // Done here (interrupts disabled) to prevent a timer from
      // incrementing ticks between the reset and the first dispatch.
      if(needs_ticks_reset){
        acquire(&tickslock);
        ticks = 0;
        release(&tickslock);
        // Don't clear the flag here — clear it only when an RT task is
        // actually dispatched.  If the parent is still mid-wakeup (no RT
        // task RUNNABLE yet), the flag stays set so the next iteration
        // resets ticks again, preventing a stale-tick first dispatch.
      }

      // Wake sleeping RT tasks whose period has arrived
      wake_sleeping_rt_tasks();

      struct sched_result sr;
      if(sched_algo == SCHED_EFDF)
        sr = schedule_efdf();
      else if(sched_algo == SCHED_PRIORITY)
        sr = schedule_priority();
      else  // SCHED_DBP
        sr = schedule_dbp();

      if(sr.process && sr.allocated_time == 0){
        // ── MISS IN DISPATCH ──
        acquire(&sr.process->lock);
        on_cycle_ended(sr.process, 1); // deadline_miss = 1
        printf("process#%d misses a deadline at %d in scheduler\n",
               sr.process->pid, ticks);
        move_to_next_cycle_sleep(sr.process);
        release(&sr.process->lock);
        continue;  // re-run wake + schedule (task now sleeping or done)
      }

      if(sr.process){
        // ── NORMAL DISPATCH ──
        needs_ticks_reset = 0;
        dispatch(c, sr);
      } else {
        // No RT task runnable; run non-RT tasks (init, sh, etc.)
        struct sched_result rr = schedule_rr();
        if(rr.process)
          dispatch(c, rr);
        else
          asm volatile("wfi");
      }
    } else {
      // Default round-robin scheduler
      struct sched_result rr = schedule_rr();
      if(rr.process)
        dispatch(c, rr);
      else
        asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
