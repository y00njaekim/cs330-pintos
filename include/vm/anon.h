#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "devices/disk.h"
struct page;
enum vm_type;

struct anon_page {
    // disk에서의 위치는 disk_sector_t로 관리되어야 한다.
    disk_sector_t swap_loc;    // swap out 시에 swapped_location 저장

};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
