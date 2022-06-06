/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "include/threads/vaddr.h"
#include "include/threads/mmu.h"
#include <string.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *disk_bitmap;
// static struct bitmap *swap_table;	// CHECK: 전역변수로 할당하는 것 옳나? swap_disk와 대응되므로 타당해 보임.
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	/* 2022.05.20 */
	// anonymous page는 backup storage가 없으므로 swap out 되어 나갈 swap_disk가 필요하다.
	// disk.c의 disk_get 함수를 살펴보면 1,1 옵션이 swap을 의미.
	// 사용법은 filesys_init의 filesys_disk 초기화 방법 참고
	swap_disk = disk_get(1,1);
	// swap_disk는 어떤 영역이 free인지, in-use인지 구분이 필요하다. 
	// 이는 swap_disk의 각 비트에 대응되는 swap_table을 비트맵으로 만듦으로써 해결.
	// 왜 bitmap? -> bitmap의 각 비트는 t/f로 swap disk의 상태를 대응하여 표현하기에 좋다.
	// free-map.c의 사용법을 참고.
	swap_table = bitmap_create(disk_size(swap_disk));	// QUESTION: swap_disk의 크기는?
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	memset(&page->uninit, 0, sizeof(struct uninit_page));
	page->operations = &anon_ops;	// anon swap in/out/destroy/type설정
	// QUSETION: struct page의 모든 원소 initialize 해주어야 하나? uninit_new 처럼?
	// page->frmae->va = kva;	
	// page->rw = true;
	struct anon_page *anon_page = &page->anon;
	anon_page->swap_loc = BITMAP_ERROR;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;	
	/* 일단, swap-in은 swap-out이 해당 페이지에 in 명령 이전에 일어났었다 가정.
	 * (1) swap-out된 위치를 기록하는 주소에 접근하여, swap-out된 기록이 없으면 false.
	 * (2) swapped_location 가져오면, 그 위치부터 disk_read
	 * (3) swap_table에 해당 disk부분 할당 해제된 것으로 표시
	 */
	disk_sector_t swapped_location = anon_page->swap_loc;
	if(swapped_location == BITMAP_ERROR) return false;	// (1) swap out된 적이 없습니다! 

	disk_sector_t disk_offset;
	void *buffer;
	struct thread *curr = thread_current();
	buffer = kva;
	disk_offset = swapped_location;
	int iteration = PGSIZE/DISK_SECTOR_SIZE;
	while(iteration) {
		disk_read(swap_disk, disk_offset, buffer);								// (2)
		disk_offset += 1;
		buffer += DISK_SECTOR_SIZE;
		iteration--;
	}
	bitmap_set_multiple(swap_table, anon_page->swap_loc, PGSIZE/DISK_SECTOR_SIZE, false);	// (3)
	return true;
}
/* 이홍기의 생각 
 * swap_in을 할때, disk를 비워줘야 하지 않을까?
 * swap_in에서 disk_read를 하고 disk를 비워주지 않으면,
 * swap_out을 할 때 disk 부분을 쓰지 못하게 되고, 그러면 swap_out 시에 다른 부분에 기록하게 되므로
 * disk에 사본이 여러개 생긴다. 그러면, swap in&out을 100번 실행한다면, disk 내에 100개의 사본이 생기는데 
 * 그러면 너무 공간을 낭비하게 되는 것 아닌가?
 * (1) disk를 냅두는 시나리오
 * 	disk를 냅두는 대신, 해당 page에 대해 swap_out이 다시 일어나게 되는 경우에는
 *  새로 disk_write하지 않고, anon_page->swap_loc이 NULL이 아니라면 copy 건너뛰고 pml4_clear_page 만 해준다
 *  PROS: swap_out과 in을 자주 반복하는 경우 비용이 싸진다.
 *  CONS: swap_out과 in을 한번만 하더라도 disk에 영구적인 저장이 되므로, 디스크 공간 최적화가 이루어지지 않는다.
 * 
 * (2) disk를 매번 초기화 해주는 시나리오
 *  swap_in 시에 disk_read 후 해당 disk 부분의 값을 초기화해준다.
 *  PROS: swap_in / out을 여러번 반복하더라도 disk 공간을 최적화할 수 있다.
 *  CONS: swap_in / out을 여러번 반복하더라도 매번 새로운 명령을 하는 것과 같다. 
 * 
 * 결론
 * 생각해보니. disk의 값을 초기화하지 않더라도 swap_table에 false로만 바꿔두면,
 * 해당 disk 영역을 덮어쓰기함으로써 초기화된 영역을 사용하는 것과 같은 효과를 낼 수 있다. 
 * 즉, 그냥 swap_table에만 disk 내용이 할당 해제된것처럼 간주하고 쓰면 된다.
 */


/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	/* 절차
	 * (1) swap out시에 free swap slot 찾는다. HOW?
	 * (2) copy the page of data into the slot
	 * (3) 데이터의 위치를 page struct에 저장 (swapped location)
	 * (4) 더 이상의 free slot이 없다면, panic the kernel
	 */
	disk_sector_t swapped_location;
	disk_sector_t disk_offset;	// QUSETION: disk_offset은 bitmap의 위치와 같은가?
	void *buffer;	// memory -> buffer -> disk 자료형 무엇?
	struct thread *curr = thread_current();
	/* PUSEDO */
	// (1) free swap slot 찾고, 없으면 panic the kernel
	swapped_location = find_free_slot_in_swap_disk();
	if(swapped_location == BITMAP_ERROR) return false; // PANIC("no free swap slot in the swap disk");	// frame과 다르게, disk에서 free 공간 없으면 PANIC (4)
	// (2) copy the page of data into the slot
	/* disk_write을 이용해 buffer = memory에서 disk로 데이터를 쓰는건 맞는 것 같음.
	 * 그런데, disk는 disk_sector 단위로 쓰고, page는 PGSIZE 단위로 쓴다. 이것을 어떻게 sync?
	 * 또한, 쓰고 나서 swapped out 된 페이지는 초기화되어야 하는데, 이건 어디서??
	 */
	/* 생각 정리 이후
	 * 메모리의 시작주소부터 page단위의 데이터를 copy하는법
	 * [1] 메모리에서 버퍼로 데이터 복사
	 * [2] 버퍼에서 디스크로 disk_write 
	 * [3] 메모리 free (free인지 0으로 초기화하는건지 확인할것)
	 */

	buffer = page->frame->kva;
	disk_offset = swapped_location;	// swapped_location이 disk_offset과 같은가? 
	int iteration = PGSIZE/DISK_SECTOR_SIZE;	// PGSIZE = DISK_SECTOR_SIZE * 8
	while(iteration) {
		// [1],[2] 잘못생각했다.
		// 버퍼의 주소를 메모리 주소로 주면, 알아서 그 주소로부터 DISK_SECTOR_SIZE만큼 복사
		disk_write(swap_disk, disk_offset, buffer);	// 버퍼 주소부터 512비트를 copy한다.
		disk_offset += 1;
		buffer += DISK_SECTOR_SIZE;
		iteration--;
	}
	// [3] page를 free
	// palloc_free_page or pml4_clear_page?
	// palloc을 해버리면 page 자체 공간이 할당 해제되는데, frame의 내용만 copy되고 초기화되는 것이므로 pml4_clear_page가 적합해보이지만,
	// pml4_clear_page는 not_present로만 바꿔준다.
	// not_present는 page_fault에서 쓰였던 인자이므로 PF 관련 이슈 생기면 여기부터 확인해보기!
	pml4_clear_page(curr->pml4, page->va);
	pml4_set_dirty(curr->pml4, page->va, false);
	// (3) 데이터의 위치를 page struct에 저장
	anon_page->swap_loc = swapped_location;
    page->frame = NULL;
	return true;


/* palloc_free_page에 대한 고찰
 * palloc_free_page는 pool로부터 page(virtual, or physical)를 가져올 때 그 링크를 끊어주는 것이고,
 * 실제 vm_dealloc_page를 살펴보면 free(frame)을 통해 공간을 할당 해제해주는 것을 알 수 있다.
 * 따라서 위의 경우 page(virtual page)와 frame(physical page)의 관계를 끊어주는 pml4_clear_page를 써야 하고,
 * palloc_free_page는 실제 page/frame을 해제(destroy)할때 쓰는 것이 맞다.
 */

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	ASSERT(VM_TYPE(page->operations->type) == VM_ANON);
	// hash_delete(&thread_current()->spt.pages, &page->hash_elem);
	/* However, modifying hash
   * table H while hash_clear() is running, using any of the
   * functions hash_clear(), hash_destroy(), hash_insert(),
   * hash_replace(), or hash_delete(), yields undefined behavior,
   * whether done in DESTRUCTOR or elsewhere. */
    if (page->frame != NULL) {
        list_remove(&(page->frame->frame_elem));
        free(page->frame);
    }
    if(anon_page -> swap_loc != BITMAP_ERROR) bitmap_set_multiple(swap_table, anon_page->swap_loc, PGSIZE/DISK_SECTOR_SIZE, false);
}
