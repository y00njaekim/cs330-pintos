/* file.c: Implementation of memory backed file object (mmaped object). */

#include <debug.h>
#include <string.h>
#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include "filesys/file.h"

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
	// memset(&page->uninit, 0, sizeof(struct uninit_page));
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	// file_page->swap_loc = NULL;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	// printf("스왑인 들어왔어요\n");
	// DEBUG
	if (page == NULL) return false;

	struct file_page *file_page = &page->file;
	bool success = true;
	off_t size = file_page->aux->page_read_bytes;
	success = file_read_at(file_page->file, kva, size, file_page->swap_loc) == size;	// bitmap.c bitmap_read 참고
	memset(kva+file_page->aux->page_read_bytes, 0, file_page->aux->page_zero_bytes);	// 꼬다리 안채워진 부분 0으로 초기화
	return success;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread *curr = thread_current();
	// printf("스왑아웃 들어왔어요\n");
	if(pml4_is_dirty(curr->pml4, page->va)) {
		file_seek(file_page->file, file_page->aux->ofs);
		printf("ofs: %d\n", file_page->aux->ofs);
		file_write(file_page->file, page->va, file_page->aux->page_read_bytes);
		//file_write_at(file_page->file, page->va, file_page->aux->page_read_bytes, file_page->aux->ofs);	// PGSIZE 맞나? 만약 페이지 내에 일부분만 쓰는거라면? page_read_bytes?
		pml4_set_dirty(curr->pml4, page->va, false);
	}
	pml4_clear_page(curr->pml4, page->va);
	file_page->swap_loc = file_page->aux->ofs;	// 오프셋 aux로 저장중이면 필요없지않나?
	page->frame = NULL;
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	// printf("destroy 진입\n");
	struct file_page *file_page UNUSED = &page->file; // type casting
	struct aux_do_mmap *aux_copy = file_page->aux;
	/* Memory Mapped Files
	 * close하고, dirty 경우 write back the changes into the file
	 * page struct를 free할 필요 없다 - caller가 할 일 */
	/* PSEUDO */
	if(pml4_is_dirty(thread_current()->pml4, page->va)) {
		// Yoonjae's Question: write 가 실패하는 경우는 없나?
		file_write_at(file_page->file, page->frame->kva, aux_copy->page_read_bytes, aux_copy->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}
	// hash_delete(&thread_current()->spt.pages, &page->hash_elem);
	/* However, modifying hash
   * table H while hash_clear() is running, using any of the
   * functions hash_clear(), hash_destroy(), hash_insert(),
   * hash_replace(), or hash_delete(), yields undefined behavior,
   * whether done in DESTRUCTOR or elsewhere. */
	// free(file_page->aux);
	if(page->frame != NULL) {
		list_remove(&(page->frame->frame_elem));
		free(page->frame);
	}
	// file_close(file_page->file);	// ?: file_page 구조체 내의 file 가리키는 인자
	// DONE: fd_table 닫아 줄 필요 있을까? 없을 듯 (with syscall close)
}

static bool
lazy_do_mmap (struct page *page, void *aux) {
	struct aux_do_mmap *aux_copy = aux; // type casting
	ASSERT(VM_TYPE(page->operations->type) == VM_FILE);
	file_seek(aux_copy->file, aux_copy->ofs);
	void *kpage = page->frame->kva;
	if (file_read(aux_copy->file, kpage, aux_copy->page_read_bytes) != (int)aux_copy->page_read_bytes) {
		return false;
	}
	memset (page->va + aux_copy->page_read_bytes, 0, aux_copy->page_zero_bytes);	// kpage 대신 page의 frame에 있는 kva
	page->file.aux = aux_copy;
	page->file.file = aux_copy->file;
	pml4_set_dirty(thread_current()->pml4, page->va, false);
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
	uintptr_t mmaped_va = addr;
	struct file *file_copy = file_reopen(file);
	if(file_copy == NULL) return NULL;
	size_t filelength = file_length(file);
	if(length > filelength) length = filelength;

	void *addr_copy = addr;

	uint32_t read_bytes = length;
	uint32_t zero_bytes = PGSIZE - read_bytes % PGSIZE;
	// ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	// ASSERT (pg_ofs (addr) == 0);
	// ASSERT (offset % PGSIZE == 0);

	while(read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct aux_do_mmap *aux = malloc(sizeof(struct aux_do_mmap));
		if(aux == NULL) return NULL;
  	// Yoonjae's Question: reopen 필요한 거 맞아?
		aux->file = file_copy; // QUESTION: 바뀐 offset 반영 위해 file_reopen(file) ?
		aux->mmaped_va = mmaped_va;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		aux->ofs = offset;

		if(!vm_alloc_page_with_initializer(VM_FILE, addr,
					writable, lazy_do_mmap, aux)) {
			file_close(file_copy);
			return NULL;
		}

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
	struct thread *curr = thread_current();

	// while(addr에 매칭된 페이지의 aux의 page_read_bytes == PGSIZE) {
	// 		spt_remove_page(spt, page);
	// }
	// if(addr에 매칭된 페이지의 aux의 page_read_bytes != 0) {
	// 		spt_remove_page(spt, page);
	// }
	uintptr_t addr_copy = (uintptr_t)addr;
	struct page *page = spt_find_page(&curr->spt, (void *)addr_copy);
	if (page == NULL) return exit(-1);
	ASSERT(page->file.aux->mmaped_va == addr);
	ASSERT(page->operations->type == VM_FILE);

	struct file *file_copy = page->file.file;
	while (page != NULL && page->file.aux->page_read_bytes == PGSIZE){
		// Yoonjae's Question: hash_delete 가 필요한 이유??
		hash_delete(&curr->spt, &page->hash_elem);
		spt_remove_page(&curr->spt, page);
		addr_copy += (uintptr_t)PGSIZE;
		page = spt_find_page(&curr->spt, addr_copy);
	}
	if(page->file.aux->page_read_bytes != 0) {
		hash_delete(&curr->spt, &page->hash_elem);
		spt_remove_page(&curr->spt, page);
	}
	file_close(file_copy);

	// TODO: munmap 에서 page at addr 의 type 이 VM_FILE 인지 체크
	// addr 이 file 의 중간에 있을 때 위 아래쪽의 file 은 어캐 detect?
	// struct page *page = NULL;
	// struct thread *curr = thread_current();
	// struct supplemental_page_table *spt = &curr->spt;

	// // 일단 unmap할 페이지를 찾는다
	// page = spt_find_page(spt, addr);
	// // 만약 used bit 있으면
	// if(pml4_is_dirty(curr->pml4, addr)) {
	// 		file_write_at();
	// }
	// spt_remove_page(spt, page);
}
