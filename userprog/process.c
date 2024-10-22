#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list.h>
#include "lib/user/syscall.h"
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#include "threads/malloc.h"
#endif

static struct lock load_lock;
static struct semaphore load_sema;
void load_sema_init(void);

void load_sema_init(void) {
	sema_init(&load_sema, 1);
}

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
struct thread *find_child(tid_t child_tid);

void argument_passing(void **p_rsp, char **argv, int argc);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* Create a new thread to execute FILE_NAME. */
	char thread_name[16];
	for (int i = 0; i < sizeof thread_name; i++) {
		if(file_name[i] == ' ') {
			thread_name[i] = '\0';
			break;
		}
		thread_name[i] = file_name[i];
	}
	tid = thread_create (thread_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	struct thread *curr = thread_current();
	tid_t tid = thread_create(name,
														PRI_DEFAULT, __do_fork, curr);
	sema_down(&curr->fork_sema);
	/* Clone current thread to new thread.*/
	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if(is_kernel_vaddr(va)) return true;	// TODO: true or false?

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if(parent_page == NULL) return false;	// unmapped?

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER|PAL_ZERO);
	if (newpage == NULL) return false;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;
	/* TODO: You don't need to clone the value of the callee-saved registers.
	 * callee-saved registers: rbx, rbp, rsp, r12 - 15
	 * 위의 TODO 해결 위해 struct thread의 intr_frame tf를 참조하여 복사.
	 * Question 1: parent->tf 참조하면 process_fork()의 if_가 참조되는가?
	 * Question 2: if 참조 시 모든 레지스터 참조될 텐데, callee-saved register만 참조되도록 설정해야 하는가?
	 */
	parent_if = &parent->ff;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0;	// In child process, the return value should be 0

	// 	/* Call the kernel_thread if it scheduled.
	//  * Note) rdi is 1st argument, and rsi is 2nd argument. */
	// t->tf.rip = (uintptr_t) kernel_thread;
	// t->tf.R.rdi = (uint64_t) function;
	// t->tf.R.rsi = (uint64_t) aux;
	// t->tf.ds = SEL_KDSEG;
	// t->tf.es = SEL_KDSEG;
	// t->tf.ss = SEL_KDSEG;
	// t->tf.cs = SEL_KCSEG;
	// t->tf.eflags = FLAG_IF;

	/* 2. Duplicate PT */
	/* TODO: protection required (memory access) */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	// TODO: fd_table[0] (STDIN), fd_table[1] (STDOUT)의 경우에도 duplicate 괜찮은가?
	current->fd_table[0] = parent->fd_table[0];
	current->fd_table[1] = parent->fd_table[1];
	for (int fd_step = 2; fd_step < FD_MAX; fd_step++) {
		// 만약 복사해야할 엔트리가 비어있다면
		if(parent->fd_table[fd_step] == NULL) continue; 
		current->fd_table[fd_step] = file_duplicate(parent->fd_table[fd_step]);
	}
	current->fdx = parent->fdx;

	#ifdef EFILESYS
	if(parent->wdir != NULL) {
		if(current->wdir != NULL) dir_close(current->wdir);
		current->wdir = dir_reopen(parent->wdir);
	} else {
		if(current->wdir != NULL) dir_close(current->wdir);
		current->wdir = dir_open_root();
	}
#endif
	sema_up(&parent->fork_sema);

	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	current->exit_status = TID_ERROR;
	sema_up(&parent->fork_sema);
	exit(TID_ERROR);
}

/* Customized */
void
argument_passing(void **p_rsp, char** argv, int argc) {

	int i;
	for (i=argc-1; i>=0; i--) {
		int arg_len = strlen(argv[i]) + 1;
		*p_rsp -= arg_len;

		uintptr_t rsp_ = *(uintptr_t *)p_rsp;

		int j;
		for(j=0; j<arg_len; j++) {
			*(char *)rsp_ = argv[i][j];
			rsp_++;
		}

		argv[i] = *(char **)p_rsp; // REMEMBER : 자료형
	}

	int padding = *(unsigned long int*)p_rsp % 8;

	int k;
	for(k=padding; k>0; k--) {
		(*p_rsp)--;
		**(uint8_t **)p_rsp = (uint8_t)0;
	}

	(*p_rsp) -= sizeof(char *);
	**(char ***)p_rsp = (char *)0;

	for (i = argc-1; i>=0; i--) {
		(*p_rsp) -= sizeof(char *);
		**(char ***)p_rsp = argv[i];
	}

	// (5) return address
	*p_rsp -= sizeof(void *);
	**(void***)p_rsp = (void *)0;

}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {

	/* TODO
		1. tokenize 할 때 file name 복사본 사용
		2. load, pallog_free_page 할 때 인자 결정 
		3. hex_dump 세 번째 argument 결정 */

	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	// QUESTION: sema_up 위치가 여기가 맞나?
	// sema_up(&thread_current()->wait_sema);
	// sema_down(&thread_current()->cleanup_sema);

	/* We first kill the current context */
	process_cleanup ();
	#ifdef VM
		supplemental_page_table_init(&thread_current()->spt);
	#endif

	/* And then load the binary */
	// lock_acquire(&load_lock);
	success = load(file_name, &_if);
	// lock_release(&load_lock);

	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}

struct thread *
find_child(tid_t child_tid) {
	struct thread *curr = thread_current();
	struct thread *child = NULL;
	struct list_elem *child_list_elem = NULL;

	for (child_list_elem = list_begin(&curr->child_list); child_list_elem != list_end(&curr->child_list); child_list_elem = list_next (child_list_elem)) {
		child = list_entry(child_list_elem, struct thread, child_elem);
		if(child->tid == child_tid)
			break;
	}
	return child;
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */

int
process_wait (tid_t child_tid) {
	/* TID와 PID?
	 * syscall에서는 PID를 통해 추적하지만 핀토스에서는 process와 thread가 대응되므로
	 * PID를 요구하는 syscall에 tid를 대입하는 것이 당위적이다. 
	 */
	
	/* PSUEDO */

	// child list 에 넣거나 fork 빼는 exit (exec) 과정 추가 
	struct thread *parent = thread_current();
	struct thread *child = find_child(child_tid);
	if(child == NULL)
		return -1; // child_tid 에 해당하는 프로세스가 child_list 에 없음

	sema_down(&child->wait_sema);
	int child_status = child->exit_status;
	list_remove(&child->child_elem); // parent 의 child list 에서 child 제거
	sema_up(&child->cleanup_sema);

	return child_status;

	// QUESTION: wait을 2회 이상 하면? 겹치면??

	/* 필요한 개념
	 * 1. 자식 리스트: 스레드 자료구조 내에 나의 자식들의 리스트가 있어야 함. (TID?)
	 * 2. 자식 리스트 elem: 서치 위해서 elem 만들어놓기
	 */

	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	// TODO: close() system call 사용하여 모든 열린 fd 닫기
	for (int fd_step = 2; fd_step < FD_MAX; fd_step++) {
		if (curr->fd_table[fd_step] == NULL) continue;
		close(fd_step);
	}
	if(curr->loaded_file != NULL) {
		file_close(curr->loaded_file);
	}
	#ifdef EFILESYS
	if(curr->wdir != NULL) dir_close(curr->wdir);
	#endif

	// QUESTION: sema_up 위치가 여기가 맞나?
	sema_up(&curr->wait_sema);
	sema_down(&curr->cleanup_sema);

	/* QUESTION: process termination message는 exit() 시스템 콜에서 불리니 print 필요 없나? */
	// printf ("%s: exit(%d)\n", ...);
	process_cleanup ();
	#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Customized */

	if(t->loaded_file != NULL) {
		file_close(t->loaded_file);
		t->loaded_file = NULL;
	}

	char *argv[64];
	int argc = 0;

	char *token;
	char *save_ptr;
	token = strtok_r(file_name, " ", &save_ptr);
	while (token != NULL) {
		argv[argc] = token;
		token = strtok_r(NULL, " ", &save_ptr);
		argc++;
	}
	argv[argc] = '\0';

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	sema_down(&load_sema);
	file = filesys_open(file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		sema_up(&load_sema);
		goto done;
	}
	file_deny_write(file);
	sema_up(&load_sema);

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
				 || ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	void **p_rsp = &if_->rsp;	// CPU 로부터 push 된 rip 와 rsp 는 R 해당 없음
	argument_passing(p_rsp, argv, argc);

	/* Customized Lab 2-1 (4) */
	if_->R.rdi = argc; // rdi, rsi는 gp_register R 명시
	if_->R.rsi = if_->rsp + sizeof(void *);

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	if(success) {
		t->loaded_file = file;
	} else {
		file_close(file);
	}

	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false; // false if disk read error occurs.
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */

	/* TODO:
	 * file_seek (file, ofs); 추가 (`load_segment` 에서 하는게 맞는 듯 - yoonjae)
	 * ofs는 어디서? aux를 통해 전달! (구조체를 vm.h에 정의)
	 * file, ofs, page_read_bytes, zero_bytes, writeable
	 * aux에서 접근할 수 있도록 하는 자료구조 찾아보기 (이미 정의되어 있는가?)
	 */

	// QUESTION: void *kva = page->frame->kva
	// TODO: 기존 load_segment에 있었던 로딩 부분을 lazy_load_segment에서 구현
	// 기존 page load 부분에서 kpage 등을 주어진 page를 이용해 수정
	/* Load this page. */
	struct aux_load_segment *aux_copy = aux;
	file_seek(aux_copy->file, aux_copy->ofs);
	void *kpage = page->frame->kva;
	if (file_read(aux_copy->file, kpage, aux_copy->page_read_bytes) != (int)aux_copy->page_read_bytes)
	{
		// Yoonjae's Question: file_read 에서 에러핸들링? page 삭제? page->frame 삭제?

		// vm_alloc_page_with_initializer 에서 할당을 하는데,
		// 본 함수에서 할당하지는 않으니까 palloc 부분은 기존과 다르게 필요없다.
		// palloc 해주는게 맞는듯 - yoonjae
		free(aux_copy);
		return false;
	}
	// Yoonjae's TODO: aux free 해줘도 되나?
	memset (kpage + aux_copy->page_read_bytes, 0, aux_copy->page_zero_bytes);	// kpage 대신 page의 frame에 있는 kva

	/* Add the page to the process's address space. */
	// if (!install_page (page, kpage, writable)) { // TRY: 1st parameter 로 page 를 넣어야 할지 page->va 를 넣어야 할지? page 인거 같긴 함
	// 	printf("fail\n");
	// 	palloc_free_page (kpage);
	// 	return false;
	// }
	free(aux_copy);
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */



static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct aux_load_segment *aux = malloc(sizeof(struct aux_load_segment));
		// Yoonjae's Question: aux 해제는 어디서?
		if(aux == NULL) return false;
		aux->file = file;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		aux->ofs = ofs;
		// TODO: 여기서 aux 설정해서 ofs 같은거 넘겨줘야함
		// 새로 자료구조 만들어야하나? 만들어야 할듯 aux자료구조 ㄱㄱ
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		ofs += page_read_bytes;
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	/* TODO: GitBook 참고하여 작성함
	 * 첫번째 스택 페이지는 lazily하게 할당될 필요 없으므로, 
	 * 로드타임에 할당하고 초기화한뒤, 마커로 표시 */
	/* 기존 코드와 같은 역할, 다른 방법이므로 기존 코드를 reference로 옆에 주석 달아두겠음 */

	if(!vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, true)) return false;
	success = vm_claim_page(stack_bottom);

	// uint8_t *kpage = palloc_get_page(PAL_USER | PAL_ZERO); // kpage palloc 대신에 vm_alloc_page_with_initializer 사용하면 될 듯
	// if (kpage != NULL) {
		// vm_alloc_page_with_initializer에서 type 항에 marker 표시 해주기
		// if(!vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, true)) return false;
		// success = install_page (stack_bottom, kpage, true);	// 이게 위에 있는 stack_bottom에 들어감.
		// success = vm_claim_page(stack_bottom);
		// 기존에는 install_page(stack_bottom) 했는데, 여기 첫번째 항이 stack_bottom
		// 첫번째 항 stack_bottom이 upage에 들어갔으니까, 바뀐 코드에서도 upage 대신에 stack_bottom 넣으면 될듯
		// 무슨말이냐면, 위에 있는 vm_alloc_page_with... 여기 두번째 항 upage에 stack_bottom 넣는다는 뜻
		// 여기서는 install_page 대신에 vm_claim_page 하면 될듯?	
		// vm_claim_page(va) 에서 va도 page claim 부르는 주소니까 스택바텀이 맞다.
	if (success) {
		if_->rsp = USER_STACK;
		thread_current()->stack_ceiling = (uintptr_t)stack_bottom; // 이건 왜 하는거지?
	}
	else {

		// Yoonjae's TRY: frame free
		// vm_dealloc_frame(spt_find_page(&thread_current()->spt, stack_bottom)->frame);
		struct page *p = spt_find_page(&thread_current()->spt, stack_bottom);
		if (p != NULL) spt_remove_page(&thread_current()->spt, p);
	}
		
		// palloc_free_page (kpage);	// 요건 마찬가지로 필요없음 아닌가 필요할수도 저 위에처럼 구현하면
	return success;
	// return success;

	/* 참고용 기존 셋업스택 코드 (project2) */
	// uint8_t *kpage;
	// bool success = false;
}
#endif /* VM */