/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "include/threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	file_page->swap_loc = NULL;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	file_read_at(file_page->file, kva, PGSIZE, file_page->swap_loc);
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	file_write_at(file_page->file, page->frame->kva, PGSIZE, 오프셋);
	pml4_clear_page(&thread_current()->pml4, page);
	file_page->swap_loc = 오프셋;
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	/* Memory Mapped Files
	 * close하고, dirty 경우 write back the changes into the file
	 * page struct를 free할 필요 없다 - caller가 할 일 */
	/* PSEUDO */
	close(file_page->file);	// ?: file_page 구조체 내의 file 가리키는 인자
}

static bool
lazy_do_mmap (struct page *page, void *aux) {
	struct aux_load_segment *aux_copy = aux;
	file_seek(aux_copy->file, aux_copy->ofs);
	void *kpage = page->frame->kva;
	if (file_read(aux_copy->file, kpage, aux_copy->page_read_bytes) != (int)aux_copy->page_read_bytes) {
		return false;
	}
	memset (kpage + aux_copy->page_read_bytes, 0, aux_copy->page_zero_bytes);	// kpage 대신 page의 frame에 있는 kva
	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// Yoon
	// 2022.05.18
	// load_segment와 같이, length 길이의 파일 요소를 PGSIZE 단위로 끊어서 입력한다. 
	// 1. length가 불러온 파일의 길이보다 길면 error -> 어떻게 처리?
	// if(length > file_length(file) || length < 0) return NULL;
	size_t filelength = file_length(file);
	if(length > filelength) length = filelength;

	void *addr_copy = addr;

	uint32_t read_bytes = length;
	uint32_t zero_bytes = 0;
	// ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	// ASSERT (pg_ofs (addr) == 0);
	// ASSERT (offset % PGSIZE == 0);

	while(read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct aux_load_segment *aux = malloc(sizeof(struct aux_load_segment));
		if(aux == NULL) return NULL;
		aux->file = file;							// QUESTION: 바뀐 offset 반영 위해 file_reopen(file) ?
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		aux->ofs = offset;

		if(!vm_alloc_page_with_initializer(VM_FILE, addr,
					writable, lazy_do_mmap, aux)) return NULL;

		/* Advance. */
		offset += page_read_bytes;
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;	
	}
	return addr_copy;
}

/* Do the munmap */
void
do_munmap (void *addr) {
}
