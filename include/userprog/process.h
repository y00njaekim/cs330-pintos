#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct aux_load_segment {
  struct file *file;
  uint32_t page_read_bytes;
  uint32_t page_zero_bytes;
  off_t ofs;
  bool writable;
};

tid_t process_create_initd(const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
