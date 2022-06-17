#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct aux_do_mmap {
  struct file *file;
	uintptr_t mmaped_va;
	uint32_t page_read_bytes;
	uint32_t page_zero_bytes;
  off_t ofs;
  bool writable;
};
struct file_page {
	struct file *file;	// file_backed_destroy에서 &page->file 에 대응하는 file 자료구조
	void *swap_loc;		// swapped location
	struct aux_do_mmap *aux; // file page 에 aux 필요한 이유 정당성: file_destroy 할 때 write_back 수행 시 필요함
};


void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
