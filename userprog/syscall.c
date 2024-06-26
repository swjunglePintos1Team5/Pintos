#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "include/lib/string.h"

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "include/threads/palloc.h"
#include "devices/input.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

//-- system call
void halt();
void exit(int);
// tid_t fork(struct intr_frame *);
tid_t fork(const char *thread_name);
bool create(const char *, unsigned);
bool remove(const char *);
int open(const char *);
int filesize(int);
int read(int, void *, unsigned);
int write(int, void *, unsigned);
void seek(int, unsigned);
unsigned tell(int);
void close(int);
int exec(const char *);
int wait(tid_t);

// struct lock *rw_lock;
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

#define MAX_OPEN_FILE 128

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

bool check_address(void *addr)
{
	struct thread *cur = thread_current();
	if (addr == NULL || !is_user_vaddr(addr) || !(pml4_get_page(cur->pml4, addr)))
	{
		return false;
	}
	return true;
}
/* The main system call interface */
/*
1. UNUSED 제거
*/
void syscall_handler(struct intr_frame *f)
{
	// TODO: Your implementation goes here.
	// 스시템콜 핸들러로 등록을 해야함 .

	int number = f->R.rax;

	switch (number)
	{
	case SYS_HALT: /* Halt the operating system. */
		halt();
		break;
	case SYS_EXIT: /* Terminate this process. */
		exit(f->R.rdi);
		break;
	case SYS_FORK: /* Clone current process. */
		memcpy(&thread_current()->fork_tf, f, sizeof(struct intr_frame));
		f->R.rax = fork(f->R.rdi);
		break;
	case SYS_EXEC: /* Switch current process. */
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT: /* Wait for a child process to die. */
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE: /* Create a file. */
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE: /* Delete a file. */
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN: /* Open a file. */
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE: /* Obtain a file's size. */
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ: /* Read from a file. */
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE: /* Write to a file. */
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK: /* Change position in a file. */
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL: /* Report current position in a file. */
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE: /* Close a file. */
		close(f->R.rdi);
		break;
	default:
		break;
	}
}

void halt()
{
	power_off();
}

void exit(int status)
{
	printf("%s: exit(%d)\n", thread_current()->name, status);
	thread_current()->exit_status = status;
	thread_exit();
}

// tid_t fork(struct intr_frame *if_)
// {
// 	return process_fork(if_->R.rdi, if_);
// }

tid_t fork(const char *thread_name)
{
	return process_fork(thread_name, &thread_current()->fork_tf);
}

bool create(const char *file, unsigned initial_size)
{

	if (file == NULL || !check_address(file))
		exit(-1);

	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	if (file == NULL || !check_address(file))
		exit(-1);

	return filesys_remove(file);
}

int open(const char *file)
{
	if (file == NULL || !check_address(file))
		exit(-1);

	// lock_acquire(&rw_lock);
	struct file *open_file = filesys_open(file);
	// lock_release(&rw_lock);

	if (open_file == NULL)
		return -1;
	struct thread *t = thread_current();
	struct file **table = t->fdt;
	int fd = t->next_fd;
	if (fd >= MAX_OPEN_FILE)
	{
		free(open_file);
		return -1;
	}

	table[fd] = open_file;
	t->next_fd = fd + 1;
	return fd;
}

int filesize(int fd)
{
	struct file *f = process_get_file(fd);
	return file_length(f);
}

int read(int fd, void *buffer, unsigned size)
{

	struct file *f;

	if (fd == 0)
	{
		// lock_release(&rw_lock);
		unsigned i;
		for (i = 0; i < size; i++)
		{
			((char *)buffer)[i] = input_getc();
		}
		return i;
	}

	f = process_get_file(fd);

	if (f == NULL || fd >= MAX_OPEN_FILE)
	{
		//	lock_release(&rw_lock);
		return -1;
	}
	else
	{
		lock_acquire(&rw_lock);
		off_t ret = file_read(f, buffer, size);
		lock_release(&rw_lock);
		return ret;
	}
}

int write(int fd, void *buffer, unsigned size)
{
	// lock_acquire(&rw_lock);
	struct file *f;

	if (fd == 1)
	{
		putbuf((char *)buffer, size);
		return size;
	}

	f = process_get_file(fd);

	if (f == NULL || fd >= MAX_OPEN_FILE)
	{
		// lock_release(&rw_lock);
		return -1;
	}
	else
	{
		lock_acquire(&rw_lock);
		off_t ret = file_write(f, buffer, size);
		lock_release(&rw_lock);
		return ret;
	}
}

void seek(int fd, unsigned position)
{
	file_seek(process_get_file(fd), position);
}

unsigned tell(int fd)
{
	return file_tell(process_get_file(fd));
}

void close(int fd)
{
	process_close_file(fd);
}

int exec(const char *file_name)
{
	check_address(file_name);

	int file_size = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (!fn_copy)
	{
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size);
	if (process_exec(fn_copy) == -1)
	{
		exit(-1);
	}
}

int wait(tid_t tid)
{

	return process_wait(tid);
}
