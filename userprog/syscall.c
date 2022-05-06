#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include "lib/user/syscall.h"
#include "threads/init.h"
#include "devices/input.h"

/* TODO : `putbuf() 는 lib/kernel/stdio.h 에 존재
	        <stdio.h> 에서 #include_next 로 lib/kernel/stdio.h 수행
					-> 우리가 따로 import 해야 하는가? */

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* TODO (DONE) : lock_init(&file_lock) 을 어디에서 해야하는 거임 !? 
	 의사결정 -> syscall_init */
static struct semaphore file_sema;

void uaddr_validity_check(uint64_t *uaddr);
struct file *fd_match_file(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	/* Customized */
	sema_init(&file_sema, 1);
}

// void halt (void) NO_RETURN;
// void exit (int status) NO_RETURN;
// pid_t fork (const char *thread_name);
// int exec (const char *file);
// int wait (pid_t);
// bool create (const char *file, unsigned initial_size);
// bool remove (const char *file);
// int open (const char *file);
// int filesize (int fd);
// int read (int fd, void *buffer, unsigned length);
// int write (int fd, const void *buffer, unsigned length);
// void seek (int fd, unsigned position);
// unsigned tell (int fd);
// void close (int fd);

// 	SYS_HALT,                   /* Halt the operating system. */
// 	SYS_EXIT,                   /* Terminate this process. */
// 	SYS_FORK,                   /* Clone current process. */
// 	SYS_EXEC,                   /* Switch current process. */
// 	SYS_WAIT,                   /* Wait for a child process to die. */
// 	SYS_CREATE,                 /* Create a file. */
// 	SYS_REMOVE,                 /* Delete a file. */
// 	SYS_OPEN,                   /* Open a file. */
// 	SYS_FILESIZE,               /* Obtain a file's size. */
// 	SYS_READ,                   /* Read from a file. */
// 	SYS_WRITE,                  /* Write to a file. */
// 	SYS_SEEK,                   /* Change position in a file. */
// 	SYS_TELL,                   /* Report current position in a file. */
// 	SYS_CLOSE,                  /* Close a file. */


/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	switch(f->R.rax) {
		case SYS_HALT:			/* Halt the operating system. */
			halt();
			break;
		case SYS_EXIT:			/* Terminate this process. */
			exit(f->R.rdi);
			break;
		case SYS_FORK:			/* Clone current process. */
			memcpy(&thread_current()->ff, f, sizeof(struct intr_frame));
			f->R.rax = fork((const char *)f->R.rdi);
			break;
		case SYS_EXEC:			/* Switch current process. */
			exec((const char *)f->R.rdi);
			break;
		case SYS_WAIT: 			/* Wait for a child process to die. */
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:		/* Create a file. */
			f->R.rax = create((const char *)f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:		/* Delete a file. */
			f->R.rax = remove((const char *)f->R.rdi);
			break;
		case SYS_OPEN:			/* Open a file. */
			f->R.rax = open((const char *)f->R.rdi);
			break;
		case SYS_FILESIZE:		/* Obtain a file's size. */
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:			/* Read from a file. */
			// is_user_vaddr(f->R.rsi);
			f->R.rax = read(f->R.rdi, (void *)f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:			/* Write to a file. */
			f->R.rax = write(f->R.rdi, (const void *)f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:			/* Change position in a file. */
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:			/* Report current position in a file. */
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:			/* Close a file. */
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}

/* 2-2 user memory access */
void 
uaddr_validity_check(uint64_t *uaddr) {
	if (((uaddr == NULL) || is_kernel_vaddr(uaddr)) || (pml4_get_page(thread_current()->pml4, uaddr) == NULL)) exit(-1);
}

/* halt
 * Terminates Pintos by calling power_off() */

void
halt(void) {
	power_off();
}

/* exit
 * Terminates the current user program, returning status to the kernel.
 * If the process's parent waits for it, this is the status that will be returned.
 * Conventionally, a status of 0 indicates success, nonzero indicates errors. */

void
exit(int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;	// thread 자료구조에 스레드 exit시 status 만들기
	printf ("%s: exit(%d)\n", thread_name(), status);	// Process termination messages
	thread_exit();
}

/* fork
*/

pid_t
fork (const char *thread_name) {
	uaddr_validity_check((uint64_t) thread_name);
	return process_fork(thread_name, &thread_current()->ff);
}

/* exec
 * 현재 프로세스를 cmd_line으로 주어진 executable로 바꾼다.
 * 성공 시 리턴하지 않고, 실패 시 exit status -1로 리턴.
 * fd는 open 상태로 유지되어야 한다. */

int
exec(const char *cmd_line) {
	// ASSERT(cmd_line != NULL);
	uaddr_validity_check((uint64_t) cmd_line);
	// TODO: cmd_line 그대로 사용하나? 
	// process_create_initd()에서 caller와 load 사이 race 방지 위해 복사. 여기서도 같은 방법?
	char *cmd_copy = palloc_get_page(0); // 복사할곳=palloc_get_page(PAL_ZERO);
	if (cmd_copy == NULL) exit(-1);		// if(복사할곳 == NULL) exit(-1);
	strlcpy(cmd_copy, cmd_line, strlen(cmd_line) + 1);	// 복사본 = strlcpy(복사할곳, cmd_line, strlen(cmd_line) + 1);
	if(debug_mode) printf("\n@@@@@@@@@@@@ EXEC FUNTION (curr pid : %d) @@@@@@@@@@@@", thread_current()->tid);
	debug_all_list_of_thread();
	if (process_exec(cmd_copy) == -1) exit(-1);		// QUESTION: palloc_free_page (cmd_copy) 해주어야 하나? 에러인 경우에?
	// TODO: 성공적으로 실행되면 자식 리스트에서 제거하여야 하나?
	NOT_REACHED ();	// thread_exit()에서의 용법 참고. exec은 exit status 없이 리턴하지 않는다.
	return 0;
}

/* wait
 */

int wait (pid_t child_tid) {
	if(debug_mode) printf("\n@@@@@@@@@@@@ WAIT FUNTION (curr pid : %d) (wait pid: %d) @@@@@@@@@@@@", thread_current()->tid, child_tid);
	debug_all_list_of_thread();
	return process_wait(child_tid);
}

/* file관련 system calls
 * file은 inode를 통해 관리된다.
 * 우리가 구현해야 할 system call들은 inode와 관련 구현된 basic 함수들을 이용해
 * fd table을 구현하고, fd를 생성 및 삭제하며 파일을 관리
 * References: file.c, filesys.c
 * fd table을 스레드 자료구조에 추가하고, filesys directory에 동시에 접근할 수 없도록 lock/semaphore */

/* create
 * 새로운 파일을 만든다 - 이름은 file, 크기는 initial_size.
 * returns true if successful, false otherwise.
 * 만든다고 바로 열리는 것은 아님. open과 별개의 operation.
 * Reference: filesys.c/filesys_create() 
 */

bool
create (const char *file, unsigned initial_size) {
	// ASSERT(file != NULL);
	uaddr_validity_check((uint64_t) file);
	// Yoonjae's Question : filesys 할 때 항상 락을 걸어야 한다면 이 경우에도 필요 하지 않나?
	return filesys_create(file, initial_size); 
}

/* remove
 * file 이라는 이름을 가진 파일을 지운다. true - success, false - otherwise.
 * open된 파일을 remove 한다고 해서 close되지 않음. 
 * Reference: filesys.c/filesys_remove()
 */ 

bool
remove (const char *file) {
	// ASSERT(file != NULL);
	uaddr_validity_check((uint64_t) file);
	return filesys_remove(file);
}

/* open
 * file 이름의 파일을 오픈.
 * 성공: fd, 실패: -1
 * Reference: filesys.c/filesys_open()
 */

int
open (const char *file) {
	// ASSERT(file != NULL);
	uaddr_validity_check((uint64_t) file);
	// (1) file 오픈 - filesys.c의 filesys_open(const char *name)
	sema_down(&file_sema);
	struct file *open_file = filesys_open(file);;
	sema_up(&file_sema);
	if(open_file == NULL) return -1;
	// (2) 해당 file에 fd 부여
	struct thread *curr = thread_current();
	while(curr->fdx < FD_MAX && curr->fd_table[curr->fdx]) curr->fdx++;	// 비어 있는 
	// (3) 성공: return fd, 실패: -1
	if(curr->fdx == FD_MAX) return -1;	// fd값이 꽉 찼습니다
	// TODO: open이 fail하는 경우의 수가 fd_table이 꽉 찬 경우 밖에 없나?

	curr->fd_table[curr->fdx] = open_file;
	return curr->fdx;
}

/* fd_match_file()
 * 해당 fd에 해당하는 file 매치
 * 경계조건들?
 */

struct file*
fd_match_file(int fd) {
	struct thread *curr = thread_current();
	// ASSERT(fd >= 0 && fd < FD_MAX);
	if(fd < 2 || fd >= FD_MAX) // YOONJAE's TRY
		exit(-1);
	return curr->fd_table[fd];
}

/* filesize
 * fd로 열린 파일의 크기를 리턴. 
 * Reference: file.c/file_length()
 */

int
filesize (int fd) {
	// (1) 해당 fd에 해당하는 file 매치
	struct file *matched_file = fd_match_file(fd);
	// NULL 체크 안하는 이유는 file_xx 함수에서 체크하기 때문에
	// (2) 성공: file 길이 리턴
	return file_length(matched_file);
}

/* read
 * fd로 열린 파일의 size 길이만큼을 buffer로 옮겨라.
 * 성공: 읽힌 바이트 수를 리턴, 실패: -1
 * fd=0(STDIN)은 input_getc()를 사용하여 keyboard로부터 읽힌다. 
 * Reference: file.c/file_read(), file.c/file_read_at()
 */

int
read (int fd, void *buffer, unsigned size) {
	uaddr_validity_check(buffer);

	int bytes_read;

	if(fd == 0) {
		// QUESTION: 매 키보드의 입력이 trigger가 되어 read(0,buf,sizeof(char)) 발생하는 것 아닌가??
		/* QUESTION:
		 * fd == 0 인경우 시스템콜 호출 시 size는 어떤 값이 들어오는가?
		 */
		 
		int size_ = size;

		// TODO: validity check
		size_t buf_len = strlen(buffer);
		while(size_--) {
			memset(buffer + buf_len, input_getc(), sizeof(char));
			buf_len++;
		}
		memset(buffer + buf_len, 0, sizeof(char));
		
		bytes_read = size;

	}
	else if(fd > 1) {
		// (2) 해당 fd에 해당하는 file 매치
		struct file *matched_file = fd_match_file(fd);
		if(matched_file == NULL) {
			return -1;
		}
		// (1) 파일에 접근할 때에는 lock 걸기
		sema_down(&file_sema);
		bytes_read = file_read(matched_file, buffer, size);
		sema_up(&file_sema);
	}

	return bytes_read;
	// (3) fd = 0:	reads from the keyboard using input_getc()
	// (4) fd != 0:	reads size bytes from the file open as fd into buffer
}

/* write
 * buffer의 size 길이만큼을 fd로 열린 파일에 쓴다.
 * 성공: 쓴 바이트 수를 리턴 (다 못쓰면 그만큼만 리턴), 실패: -1 아닐까? - 0인가?
 * 원래는 eof(end-of-file)만나면 연장해서 쓰지만, 여기서는 구현이 되어 있지 않다.
 * eof 만나면 거기까지만 쓰고, 아예 못쓰면 0
 * fd=1(STDOUT)은 putbuf()를 사용하여 buf에 저장된 데이터를 출력
 * Reference: file.c/file_write(), file.c/file_write_at()
 * 
 * 2-5 Denying writes to executables 고려 나중에 하기 
 */

int
write (int fd, const void *buffer, unsigned size) {
	uaddr_validity_check((uint64_t) buffer);
	int bytes_written = 0;
	sema_down(&file_sema);

	// (3) fd = 1:	writes on the console using putbuf()
	if(fd == 1) {
		putbuf(buffer, size); // TODO: sizeof(buffer) < size 인 경우에 putbuf while문에서 무한루프?
		bytes_written = size;	
	}	else if (fd > 1) {
		// (2) 해당 fd에 해당하는 file 매치
		struct file *matched_file = fd_match_file(fd);
		if(matched_file == NULL) {
			sema_up(&file_sema);
			return -1;
		}
		// (4) fd != 1:	writes size bytes from buffer to the open file
		bytes_written = file_write(matched_file, buffer, size);
	}
	sema_up(&file_sema);
	return bytes_written;
}

/* seek
 * fd로 열린 파일의 다음 read/write 위치 (파일 커서의 위치)를 position으로 바꾼다.
 * position = 0: 파일의 시작.
 * 파일의 현재 끝을 지난 seek은 error가 아니다 
 * -> 끝 넘어서 read하면 obtain 0 bytes (end of file 의미), write하면 extend the file(lab4 전에는 error 리턴)
 * Reference: file.c/file_seek()
 */

void
seek (int fd, unsigned position) {
	// (1) 해당 fd에 해당하는 file 매치
	// TODO: if(fd == 0 || fd == 1) return;
	struct file *matched_file = fd_match_file(fd);

	// (2) offset을 position 만큼 이동
	file_seek(matched_file, position);
}

/* tell
 * 읽거나 써야 하는 다음 바이트의 위치를 리턴. 
 * 파일 시작으로부터의 바이트 수로 표현된다. 
 * Reference: file.c/file_tell() 
 */

unsigned
tell (int fd) {
	// file_tell() 함수는 struct file *file을 인자로 받기 때문에 이를 fd로부터 구해준다.

	// (1) 해당 fd에 해당하는 file 매치
	// TODO: if(fd == 0 || fd == 1) return;
	struct file *matched_file = fd_match_file(fd);

	// (2) 해당 file의 열린 위치 반환
	return file_tell(matched_file);
}

/* close
 * fd 닫는다. process를 exit하거나 terminate 하는 것들은 자연스레 열린 모든 fd를 닫는다. 
 * process_exit() 시에 열린 파일 모두 닫아야 하는데, 이를 구현해주기
 * Reference: file.c/file_close() 
 */

void
close (int fd) {
	// (1) 해당 fd에 해당하는 file 매치
	struct thread *curr = thread_current();
	struct file *matched_file = fd_match_file(fd);
	
	// (2) file 닫고, fd entry 초기화
	curr->fd_table[fd] = NULL;
	curr->fdx =  (curr->fdx > fd) ? fd : curr->fdx;
	file_close(matched_file);
}