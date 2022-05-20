/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_table;	// CHECK: 전역변수로 할당하는 것 옳나? swap_disk와 대응되므로 타당해 보임.
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
	swap_table = bitmap_create(sizeof(swap_disk));	// QUESTION: swap_disk의 크기는?
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;	// anon swap in/out/destroy/type설정
	// QUSETION: struct page의 모든 원소 initialize 해주어야 하나? uninit_new 처럼?
	// page->frmae->va = kva;	
	// page->rw = true;
	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* find_free_slot_in_swap_disk
 * swap disk에서 빈 공간을 찾아 page의 데이터를 넣어준다.
 * 이는 swap_table을 통해 관리
 * swap_table bitmap에서 어떻게 PGSIZE의 연속된 빈 공간(marked as false)를 발견할까?
 * -> bitmap.c의 bitmap_scan 이용
 * bitmap_scan: bitmap을 돌면서, 처음으로 CNT개수의 비트가 value로 매핑되어있는 공간을 찾고 starting index 돌려준다.
 */
static size_t
find_free_slot_in_swap_disk() {
	// swap_table에서 빈 공간으로 마킹되어 있는 PGSIZE 길이의 공간을 찾고, true(할당됨)로 바꾼다.
	size_t idx = bitmap_scan_and_flip(swap_table, 0, PGSIZE, false);
	return idx;
}
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
	size_t swapped_location;
	disk_sector_t disk_offset;	// QUSETION: disk_offset은 bitmap의 위치와 같은가?
	void *buffer;	// memory -> buffer -> disk 자료형 무엇?
	/* PUSEDO */
	// (1) free swap slot 찾고, 없으면 panic the kernel
	swapped_location = find_free_slot_in_swap_disk();
	if(swapped_location == BITMAP_ERROR) PANIC("no free swap slot in the swap disk");	// frame과 다르게, disk에서 free 공간 없으면 PANIC (4)
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
	buffer = malloc(DISK_SECTOR_SIZE);
	disk_offset = swapped_location;	// swapped_location이 disk_offset과 같은가? 
	int iteration = 2;	// PGSIZE = DISK_SECTOR_SIZE * 2
	while(iteration) {
		// [1]
		메모리 -> 버퍼
		// [2]
		disk_write(swap_disk, disk_offset, buffer);
		// [3] palloc_page_free는 아닌듯? 열심히 매핑해둔 페이지를 free하는것이 아니라 page를 초기화만 하는건가?
		// sth
		disk_offset += DISK_SECTOR_SIZE;
		iteration--;
	}
	// (3) 데이터의 위치를 page struct에 저장
	anon_page->swap_loc = swapped_location;

}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
