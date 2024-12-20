#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include<stdlib.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread* idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread* initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame {
  void* eip;             /* Return address. */
  thread_func* function; /* Function to call. */
  void* aux;             /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/*浮点数相关函数*/
void fpu_init();

static void init_thread(struct thread*, const char* name, int priority);
static bool is_thread(struct thread*) UNUSED;
static void* alloc_frame(struct thread*, size_t size);
static void schedule(void);
static void thread_enqueue(struct thread* t);
static tid_t allocate_tid(void);
void thread_switch_tail(struct thread* prev);

static void kernel_thread(thread_func*, void* aux);
static void idle(void* aux UNUSED);
static struct thread* running_thread(void);

static struct thread* next_thread_to_run(void);
static struct thread* thread_schedule_fifo(void);
static struct thread* thread_schedule_prio(void);
static struct thread* thread_schedule_fair(void);
static struct thread* thread_schedule_mlfqs(void);
static struct thread* thread_schedule_reserved(void);

/* Determines which scheduler the kernel should use.
   Controlled by the kernel command-line options
    "-sched=fifo", "-sched=prio",
    "-sched=fair". "-sched=mlfqs"
   Is equal to SCHED_FIFO by default. */
enum sched_policy active_sched_policy;

/* Selects a thread to run from the ready list according to
   some scheduling policy, and returns a pointer to it. */
typedef struct thread* scheduler_func(void);

/* Jump table for dynamically dispatching the current scheduling
   policy in use by the kernel. */
scheduler_func* scheduler_jump_table[8] = {thread_schedule_fifo,     thread_schedule_prio,
                                           thread_schedule_fair,     thread_schedule_mlfqs,
                                           thread_schedule_reserved, thread_schedule_reserved,
                                           thread_schedule_reserved, thread_schedule_reserved};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void) {
  ASSERT(intr_get_level() == INTR_OFF);

  lock_init(&tid_lock);
  list_init(&ready_list);
  list_init(&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread();
  init_thread(initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid();
}

/*如果是用户进程的测试案例，进一步初始化main_thread*/
void further_thread_init()
{
  /*初始化子进程列表*/
  struct list* cp=malloc(sizeof(struct list));
  list_init(cp);
  initial_thread->child_process=cp;

  /*初始化wait系统调用的信号量*/
  sema_init(&initial_thread->wait_for_child,0);

  /*初始化exec系统调用的信号量*/
  sema_init(&initial_thread->pcb->from_child,0);
}


/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void) {
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init(&idle_started, 0);
  thread_create("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void) {
  struct thread* t = thread_current();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pcb != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void) {
  printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n", idle_ticks, kernel_ticks,
         user_ticks);
}


/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char* name, int priority, thread_func* function, void* aux) {
  struct thread* t;
  struct kernel_thread_frame* kf;
  struct switch_entry_frame* ef;
  struct switch_threads_frame* sf;
  tid_t tid;

  ASSERT(function != NULL);

  /* Allocate thread. */
  t = palloc_get_page(PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread(t, name, priority);
  tid = t->tid = allocate_tid();

  /*初始化可能正在等待的锁*/
  t->lock=NULL;

  /*初始化文件链表*/
  list_init(&t->open_files);
  /*初始化文件描述符*/
  t->cur_file_fd=3;

  /*初始化父进程与子进程通信的信号量*/
  sema_init(&t->wait_for_child,0);

  /*初始化是否等待过*/
  t->waited=false;

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame(t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame(t, sizeof *ef);
  ef->eip = (void (*)(void))kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame(t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /*初始化fpu*/
  fpu_init(t);

  /* Add to run queue.*/
  thread_unblock(t);

  /* 若是优先级调度且优先级较高则释放cpu*/
  if(active_sched_policy==SCHED_PRIO&&t->priority>thread_current()->priority)
  thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void) {
  ASSERT(!intr_context());
  ASSERT(intr_get_level() == INTR_OFF);

  thread_current()->status = THREAD_BLOCKED;
  schedule();
}
/*按照优先级插入线程*/
void priority_insert_threads(struct thread *cur,struct list*list)
{
  ASSERT(list!=NULL);
  ASSERT(&cur->elem!=NULL);

  struct list_elem*e;
  for(e=list_begin(list);e!=list_end(list);e=list_next(e))
  {
    struct thread *tmp=list_entry(e,struct thread,elem);
    if(tmp->priority<cur->priority)
    {
      list_insert(e,&cur->elem);
      return;
    }
  }
  list_push_back(list,&cur->elem);
}
/* Places a thread on the ready structure appropriate for the
   current active scheduling policy.
   
   This function must be called with interrupts turned off. */
static void thread_enqueue(struct thread* t) {
  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(is_thread(t));

  if (active_sched_policy == SCHED_FIFO)
    list_push_back(&ready_list,&t->elem);
  else if (active_sched_policy == SCHED_PRIO)
    priority_insert_threads(t,&ready_list);
  else
    PANIC("Unimplemented scheduling policy value: %d", active_sched_policy);
}

/*找到一个列表中优先级最大的线程并将其出队*/
struct thread*find_highest_priority_and_dequeue(struct list*pri_list)
{
  ASSERT(pri_list!=NULL);
  int highest_pri=-1;
  struct thread*highest=NULL;
  struct list_elem*out;
  struct list_elem*e;
  for(e=list_begin(pri_list);e!=list_end(pri_list);e=list_next(e))
  {
    struct thread*tmp=list_entry(e,struct thread,elem);
    if(tmp->priority>highest_pri)
    {
      highest_pri=tmp->priority;
      highest=tmp;
      out=e;
    }
  }
  ASSERT(highest_pri!=-1);
  list_remove(out);
  return highest;
}
/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread* t) {
  enum intr_level old_level;

  ASSERT(is_thread(t));

  old_level = intr_disable();
  ASSERT(t->status == THREAD_BLOCKED);
  thread_enqueue(t);
  t->status = THREAD_READY;
  intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char* thread_name(void) { return thread_current()->name; }

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread* thread_current(void) {
  struct thread* t = running_thread();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT(is_thread(t));
  ASSERT(t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void) { return thread_current()->tid; }

/*判断一个线程是否正在被执行*/
bool is_executing(const char*name)
{
  struct list_elem*e;
  for(e=list_begin(&all_list);e!=list_end(&all_list);e=list_next(e))
  {
    struct thread*tmp=list_entry(e,struct thread,allelem);
    if(strcmp(tmp->name,name)==0)return true;
  }
  return false;
}

/*释放所有未close的文件*/
void free_all_open_files()
{
  struct list_elem*e;
  struct thread*cur=thread_current();
  for(e=list_begin(&cur->open_files);e!=list_end(&cur->open_files);)
  {
    struct thread_file*to_be_free=list_entry(e,struct thread_file,elem_tf);
    struct list_elem*tmp=e;
    e=list_next(e);
    file_close(to_be_free->f);
    list_remove(tmp);
    free(to_be_free);
  }
  
}
/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void) {
  ASSERT(!intr_context());

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_switch_tail(). */
  intr_disable();
  list_remove(&thread_current()->allelem);
#ifdef USERPROG
  free(thread_current()->child_process);
#endif
  free_all_open_files();
  thread_current()->status = THREAD_DYING;
  schedule();
  NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void) {
  struct thread* cur = thread_current();
  enum intr_level old_level;

  ASSERT(!intr_context());

  old_level = intr_disable();
  if (cur != idle_thread)
    thread_enqueue(cur);
  cur->status = THREAD_READY;
  schedule();
  intr_set_level(old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void thread_foreach(thread_action_func* func, void* aux) {
  struct list_elem* e;

  ASSERT(intr_get_level() == INTR_OFF);

  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
    struct thread* t = list_entry(e, struct thread, allelem);
    func(t, aux);
  }
}

/* Sets the current thread's real priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority) 
{ 
  struct thread*cur=thread_current();
  ASSERT(cur->priority>=cur->real_priority);
  if(cur->real_priority==cur->priority)
  {
    cur->priority=new_priority;
    cur->real_priority=new_priority;
  }
  else if(cur->real_priority<cur->priority)
    cur->real_priority=new_priority;
  
  thread_yield();
}

/* Returns the current thread's priority. */
int thread_get_priority(void) { return thread_current()->priority; }

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED) { /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void) {
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void) {
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void) {
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void idle(void* idle_started_ UNUSED) {
  struct semaphore* idle_started = idle_started_;
  idle_thread = thread_current();
  sema_up(idle_started);

  for (;;) {
    /* Let someone else run. */
    intr_disable();
    thread_block();

    /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
    asm volatile("sti; hlt" : : : "memory");
  }
}

/* Function used as the basis for a kernel thread. */
static void kernel_thread(thread_func* function, void* aux) {
  ASSERT(function != NULL);

  intr_enable(); /* The scheduler runs with interrupts off. */
  function(aux); /* Execute the thread function. */
  thread_exit(); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread* running_thread(void) {
  uint32_t* esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm("mov %%esp, %0" : "=g"(esp));
  return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool is_thread(struct thread* t) { return t != NULL && t->magic == THREAD_MAGIC; }

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void init_thread(struct thread* t, const char* name, int priority) {
  enum intr_level old_level;

  ASSERT(t != NULL);
  ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT(name != NULL);

  memset(t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;

  strlcpy(t->name, name, sizeof t->name);
  t->stack = (uint8_t*)t + PGSIZE;
  t->priority = priority;
  t->real_priority=priority;
  /*初始化持有的锁的链表*/
  list_init(&t->locks);
  t->pcb = NULL;
  t->magic = THREAD_MAGIC;

  old_level = intr_disable();
  list_push_back(&all_list, &t->allelem);
  intr_set_level(old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void* alloc_frame(struct thread* t, size_t size) {
  /* Stack data is always allocated in word-size units. */
  ASSERT(is_thread(t));
  ASSERT(size % sizeof(uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* First-in first-out scheduler */
static struct thread* thread_schedule_fifo(void) {
  if (!list_empty(&ready_list))
    return list_entry(list_pop_front(&ready_list), struct thread, elem);
  else
    return idle_thread;
}

/* Strict priority scheduler */
static struct thread* thread_schedule_prio(void) {
  if (!list_empty(&ready_list))
    return list_entry(list_pop_front(&ready_list), struct thread, elem);
  else
    return idle_thread;
}

/* Fair priority scheduler */
static struct thread* thread_schedule_fair(void) {
  PANIC("Unimplemented scheduler policy: \"-sched=fair\"");
}

/* Multi-level feedback queue scheduler */
static struct thread* thread_schedule_mlfqs(void) {
  PANIC("Unimplemented scheduler policy: \"-sched=mlfqs\"");
}

/* Not an actual scheduling policy — placeholder for empty
 * slots in the scheduler jump table. */
static struct thread* thread_schedule_reserved(void) {
  PANIC("Invalid scheduler policy value: %d", active_sched_policy);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread* next_thread_to_run(void) {
  return (scheduler_jump_table[active_sched_policy])();
}

/*让当前进程休眠ticks时间*/
void thread_sleep(int64_t ticks)
{
  if(ticks<=0)return;
  struct thread*cur=thread_current();

  enum intr_level old_level=intr_disable(); //关闭中断
  if(cur!=idle_thread)
  {
    cur->status=THREAD_SLEEP;
    cur->wake_time=timer_ticks()+ticks;
    schedule();
  }
  intr_set_level(old_level);  //恢复中断
}

/*检查是否有休眠进程*/
void wakeup_potential_sleep_thread()
{
  struct list_elem*e;
  int64_t cur_time=timer_ticks();
  for(e=list_begin(&all_list);e!=list_end(&all_list);e=list_next(e))
  {
    struct thread*tmp=list_entry(e,struct thread,allelem);
    enum intr_level old_level=intr_disable(); //关闭中断
    if(tmp->status==THREAD_SLEEP&&tmp->wake_time<=cur_time)
    {
      /*唤醒进程*/
      tmp->status=THREAD_READY;
      thread_enqueue(tmp);
    }
    intr_set_level(old_level);//关闭中断
  }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_switch() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void thread_switch_tail(struct thread* prev) {
  struct thread* cur = running_thread();

  ASSERT(intr_get_level() == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate();
#endif

  // /* If the thread we switched from is dying, destroy its struct
  //    thread.  This must happen late so that thread_exit() doesn't
  //    pull out the rug under itself.  (We don't free
  //    initial_thread because its memory was not obtained via
  //    palloc().) */
  // if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread/*&&prev->father==initial_thread*/) {
  //   ASSERT(prev != cur);
  //   palloc_free_page(prev);
  // }
}

/* Schedules a new thread.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_switch_tail()
   has completed. */
static void schedule(void) {
  struct thread* cur = running_thread();
  struct thread* next = next_thread_to_run();
  struct thread* prev = NULL;

  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(cur->status != THREAD_RUNNING);
  ASSERT(is_thread(next));

  if (cur != next)
  {
  asm volatile (
      "fsave %0"
      :: "m" (cur->fs.fpu_registers[0]) // 将 FPU 状态保存到 fpu 结构中
  );
    asm volatile (
      "frstor %0"
      :: "m" (next->fs.fpu_registers[0]) // 将 FPU 状态保存到 fpu 结构中
  );
    prev = switch_threads(cur, next);
  }
  thread_switch_tail(prev);
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid(void) {
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire(&tid_lock);
  tid = next_tid++;
  lock_release(&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);

/*初始化fpu*/
void fpu_init(struct thread*new)
{  
  struct thread *cur=thread_current();
  asm volatile("fsave %0"::"m"(cur->fs.fpu_registers[0]));
  asm volatile("fninit");
  asm volatile("fsave %0"::"m"(new->fs.fpu_registers[0]));
  asm volatile("frstor %0"::"m"(cur->fs.fpu_registers[0]));
}
