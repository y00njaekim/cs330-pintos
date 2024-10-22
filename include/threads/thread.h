#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "filesys/file.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0 

#define	FD_MAX 128						/* Max value of fd */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Customized */
	int original_priority;
	struct list donor_list;
	struct list_elem donor_elem;
	struct lock *waiting_lock;

	struct list_elem thread_elem; /* It is in thread_list for tracking all the existing thread */
	int nice;
	int recent_cpu;

	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. It is in ready_list / waiting_list of lock / sleep_list , and so on. */

	/* Customized
	 * file-related structures */
	struct file **fd_table; // TODO : FD_MAX = 128 ?? 
	int fdx; // file open시 매번 순회하여 찾을지, 혹은 fd_max 설정 시 순회할지

	/* thread status for system call */
	int exit_status;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
	uintptr_t stack_ceiling;							/* reference process.c:881 */
	uintptr_t user_rsp;											/* reference interupt_frame member */
#endif
#ifdef EFILESYS
	struct dir *wdir;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	

	/* Customized */
	int64_t wake_up_tick;               /* Information for switching */

	/* Customized Lab 2-2 */
	struct list child_list;
	struct list_elem child_elem;

	struct intr_frame ff; // fork frame

	struct semaphore fork_sema;
	struct semaphore wait_sema;
	struct semaphore cleanup_sema;

	/* Customized Lab 2-5 */
	struct file* loaded_file;

	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

/* Customized */
extern bool debug_mode;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);
void list_insert_ordered_ready_list(struct list_elem *); /* Customized */

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_sleep (int64_t tick);
void threads_wake_up(int64_t tick);

int thread_get_priority (void);
void thread_set_priority (int);

void priority_update_all(void);
void rcpu_increment(void);
void load_avg_update(void);
void rcpu_update_all(void);

void thread_set_nice(int);
int thread_get_nice(void);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

int eval_priority(struct thread *);
int eval_recent_cpu(struct thread *);
int eval_load_avg(int);

void do_iret (struct intr_frame *tf);

bool is_loaded(const char *);

// DEBUG
void debug_list_ready_list(void);
void debug_mlfq_status(void);
void debug_all_list_of_thread(void);
void debug_list(struct list *);

#endif /* threads/thread.h */
