/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "lib/kernel/hash.h"
#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <string.h>
#include "userprog/process.h"

// static struct semaphore frame_sema;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_list);
	// sema_init(&frame_sema, 1);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT);
	
	struct page *npage;

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		/* PSUEDO */
		npage = malloc(sizeof(struct page));	// TODO: npage 이름 맞게끔 바꾸기

		// 여기에 vm 타입에 따라 마지막 항 바꿔주는것 필요
		// 마지막 항이 uninit.h에 있는 (*page_initializer) 항인듯.
		// page_initializer에 vm 타입
		// if (vm_type = VM_ANON) intializer = anon_initializer
		// CHECK: Using VM_TYPE macro defined in vm.h can be handy.
		bool (*initializer)(struct page *, enum vm_type, void *);	// 마지막 void 포인터는 address (ex)kva)
		if(VM_TYPE(type) == VM_ANON) initializer = anon_initializer;	// 얘처럼 하면 왜 안됨?
		else if(VM_TYPE(type) == VM_FILE) initializer = file_backed_initializer;
		
		// else if file backed = fildfds
		uninit_new(npage, pg_round_down(upage), init, type, aux, initializer);	// QUESTION: initializer 무엇?

		// CHECK: modify the field after calling the uninit_new
		// QUESTION: writable 설정은 어디서?
		npage->rw = writable;

		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, npage);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* TODO: Fill this function. */
	// TODO: vaddr.h 또는 mmu.h 에 있는 함수들 중에서 ASSERT해야 하는 것 있는지 확인
	return page_lookup(va);	// page_lookup 함수 옮겨오기
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	if (hash_insert (&spt->pages, &page->hash_elem) == NULL) succ = true; // null이 반환되면 제대로 insert 된 것이고, 아니면 같은 항목이 존재
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
// printf("vm_get_victim 들어왔어요\n");
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */
    /* NOTICE: vm_evict_frame에서 victim이 null인경우 에러처리 안했기 때문에 
     * 이 함수에서 victim get 못하면 NULL 반환해야 한다 */
    // 2022.05.18 
    // TODO: 가장 오랫동안 안쓰인 page 가져올건지? 단순히 FIFO로 가져올건지? policy 결정하기
    // victim page는 frame의 page list 탐색하여 가져와야 하므로 frame에 list_entry 사용할 수 있도록 list_elem 정의하기
    // 2022.05.19
    // Policy: 가장 오래된 것? 또는 최근에 쓰이지 않은 것을 victim으로. 
    // pml4_is_accessed 이용하여 최근에 쓰이지 않은 것 evict하기.
    // QUESTION: 무엇에 대해 루프 돌면서 pml4_is_accessed 불러야 하는가?
    size_t lru_len = list_size(&frame_list);
    struct list_elem *tmp = list_begin(&frame_list);
    struct frame *tmp_frame;
    struct list_elem *next_tmp;
    for (size_t i = 0; i < lru_len; i++) {
        // printf("for문 도는중입니다. %d", i);
        tmp_frame = list_entry(tmp, struct frame, frame_elem);
        if (pml4_is_accessed(thread_current()->pml4, tmp_frame->page->va)) {
            // printf("accessed가 true입니다.\n");
            pml4_set_accessed(thread_current()->pml4, tmp_frame->page->va, false);
            next_tmp = list_next(tmp);
            list_remove(tmp);
            list_push_back(&frame_list, tmp);
            tmp = next_tmp;
            continue;
        }
        if (victim == NULL) {
            // printf("victim이 NULL입니다.\n");
            victim = tmp_frame;
            next_tmp = list_next(tmp);
            list_remove(tmp);
            tmp = next_tmp;
            continue;
        }
        tmp = list_next(tmp);
    }
    if (victim == NULL)
        {victim = list_entry(list_pop_front(&frame_list), struct frame, frame_elem);}
    // /printf("victim 값: %p\n", victim);
    // printf("victim->page 값: %p\n", victim->page);
    // printf("victim kva값: %p\n", victim->kva);
    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	// if(victim->page != NULL && !swap_out(victim->page))
	// return NULL;
	if(!swap_out(victim->page))
		return NULL;
	victim->page = NULL;
	memset(victim->kva, 0, PGSIZE);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	// palloc_get_page(PAL_USER)
	void *frame_get = palloc_get_page(PAL_USER);		// TRY: 문제있으면 void *로 캐스팅 없이


	// Obtains a single free page and returns its kernel virtual address. 즉, frame_get은 frame->kva와 같다.
	// if(frame_get == NULL) return vm_evict_frame();			// evict the page and return it
	// QUESTION: vm_evict_frame()에서 error가 발생하면 null을 리턴하는데, vm_get_frame도 null 리턴하는게 맞나 또는 error handling 필요?
	// DELETE
	if(frame_get == NULL)
		return vm_evict_frame();  // PANIC("todo"); // You don't need to handle swap out for now in case of page allocation failure. Just mark those case with PANIC ("todo") for now.

	frame = malloc(sizeof(struct frame));	// TODO: 에러 핸들링
	frame->kva = frame_get;
	frame->page = NULL; 	// CHECK
	
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Customized */
void
vm_dealloc_frame (struct frame *frame) {
	palloc_free_page(frame->kva);
	free(frame);
}


/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	void *va = pg_round_down(addr);
	uintptr_t stc = thread_current()->stack_ceiling;
	thread_current()->stack_ceiling = pg_round_down(addr) < stc ? pg_round_down(addr) : stc;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct thread *curr = thread_current();
	struct supplemental_page_table *spt UNUSED = &curr->spt;
	struct page *page = NULL;

	if(is_kernel_vaddr(addr) && user)
		return false;
	if (!not_present)
		return false; // not present: write on read-only page
	if(write && !not_present) return false;

	/* Yoonjae's Check: 등호 조건 보기 */
	page = spt_find_page(&thread_current()->spt, addr);
	if(page == NULL) {
		uintptr_t rsp = user ? f->rsp : curr->user_rsp;
		if ((uintptr_t)addr > rsp - 64 && (uintptr_t)USER_STACK > (uintptr_t)addr && (uintptr_t)addr >= (uintptr_t)(USER_STACK - (1 << 20))) {

			if(!vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true)) return false;
			uintptr_t prev_stack_ceiling = curr->stack_ceiling;

			struct page *temp_page = NULL;
			int temp_addr = 0;
			for (temp_addr = prev_stack_ceiling; temp_addr >= pg_round_down(addr); temp_addr -= PGSIZE) {
				temp_page = spt_find_page(&thread_current()->spt, temp_addr);
				if(temp_page == NULL) {
					if(!vm_alloc_page(VM_ANON | VM_MARKER_0, temp_addr, true)) return false;
				}
			}
			vm_stack_growth(addr);
			page = spt_find_page(&thread_current()->spt, addr);
			ASSERT(page != NULL);
		}
		else
		{
			return false;
			// DONE: 무조건 False 맞나?
			/* 맞는듯 왜냐하면 exception.c 에서 pagefault 발생 후에
				* vm_try_handle_fault 가 false return 하면 어처피 exit(-1) 됨 */
		}
	} 
	// else { // Yoonjae's TRY
	// 	uintptr_t rsp = user ? f->rsp : curr->user_rsp;
	// 	if((uintptr_t)addr > rsp-64 || (uintptr_t)addr >= (uintptr_t)(USER_STACK - (1<<20))) return false;
	// 	else if(write && !page->rw) return false;
	// }
	if(write && !page->rw) return false;
	/* else {
		Yoonjae's comment
		페이지가 있어
		알고보니까 해당 주소에 해당하는 페이지를 예전에 만들어 놓았던 거야
		근데 그 페이지의 va 가 rsp 와 차이가 너무 커 16 bytes 는 커녕 100 bytes 정도 차이나
		이 때 bogus fault or real page fault?
		페이지가 있을 때
		stack_ceiling 보다 작은 값 (below) 에서 page 가 있을 수도 있고
		stack_ceiling 보다 큰 값에서 page 가 있을 수도 있을 듯.
	} */

	return vm_do_claim_page (page);

	/* TODO: Validate the fault */
	// bogus 중에서도 lazy_load인지? 아니면 잘못된 곳에 접근해서 발생한 pf인지?
	// is_kernel_vaddr 이거 체크해줘야 하는 이유가, 커널 영역에 접근하면 PF가 맞다
	// not_present 체크해야 하는 이유가, read-only인데 쓰면 문제가 맞다
	// 근데, user와 write 변수는 왜 신경 안써도 되냐면 -> 그 경우에 문제가 없기 때문에
	// addr 변수가 PF가 발생한 fault_address기 때문에 저거 이용해서 체크해주면 된다.

	/* TODO: Your code goes here */
	// validate 해주기
	/* TODO: 이 함수 수정해서 resolve the page struct corresponding to the faulted addr.
	 * by coinsulting to the spt through spt_find_page */
	// rsp 저장해야해.
	// (1) user모드에서 접근하는거면 바로 rsp 부르면 돼.
	// (2) kernel에서는 rsp를 따로 저장해둬야함. 
	// 저장하는 방법: GitBook 참고
	// such as saving rsp into struct thread on the initial transition from user to kernel mode.
	// -> 즉, current_thread 자료구조에 rsp 저장하는거 만드세요.
	// 그 rsp 저장하는 변수에다가 user->kernel 전환 시에 저장해놓고 커널에서 접근할때 갖다 쓰세요.
	// stack이 성장해야 하는 경우
	// find_page 했는데 이게 null인 경우 -> 진짜 segfault이거나 아니면 stack_growth 부르는 상황일텐데,
	// 이걸 어떻게 판단? TODO: rsp와 비교해서 8byte 이내에? 있으면 성장이고 아니면 잘못 접근한 것으로 판단 하는 등 heuristic 합의하기
	// 1 - USER_STACK 보다 주소가 낮으면서 (스택 영역 내에 있거나)
	// 2 - 스택의 크기가 1MB 이하이면서 (즉, addr가 USER_STACK-2^20 위에 있거나)
	// -> 1MB 보다 스택사이즈 크게 가져간 공간에 접근하려하면 General Protection fault (`exception.c:53`)
	// 3 - heuristic을 만족하면 (addr가 rsp보다 작은데 (스택 바깥에 있는데). 너무 바깥 아닌경우)
	// -> vm_stack_growth 부르세요!
	
	// vm_stack_growth 얼마나 성장시켜야 하나?
	// 만약에 한 개의 page 이상을 성장시켜야 하는 경우 - 휴리스틱에 따라 이런 경우가 생길수도 있다. 
	// 첨언하자면, 휴리스틱이 rsp 아주 밑에 있는 것도 성장해야하는 경우로 판단하는 경우
	// 이런 경우에는 addr를 page 단위로 round down 하고, 그에 맞는 만큼 stack_growth 함수로 성장시킨다
	// 하지만, 너무 밑에까지 성장시키는 휴리스틱은 잘못될 가능성이 높으므로, 스택 바텀과 큰 차이 안나는 곳까지만 성장시킬텐데
	// 그런 경우에는 page 1개만큼만 성장시키면 되니까 round down 할 필요가 없다
	// QUESTION: 비디오 참고


	// anon / file_back 확인해서 그에 맞는 initializer 실행 -> uninit_initialize
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/* [Gitbook]
 * Claims the page to allocate va.
 * You will first need to get a page and then
 * calls vm_do_claim_page with the page.
 */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = spt_find_page(&thread_current()->spt, va);
	if(page == NULL) return false;

	/* TODO: Fill this function */
	/* PSUEDO
	 * page 할당하고 (malloc(sizeof(struct page))인지 palloc_get_page 헷갈림)
	 * page->va = va
	 * 혹시 더 initialize 할 것 있으면 넣기
	 */
	
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {

	// sema_down(&frame_sema);
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	list_push_back(&frame_list, &(frame->frame_elem));

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// pml4_set_page
	// if(pml4_get_page (thread_current()->pml4, page->va) == NULL
	// 	&& pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->rw)) return swap_in (page, frame->kva);
	if(pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->rw)) return swap_in(page, frame -> kva);// { sema_up(&frame_sema); return swap_in (page, frame->kva);}

	// sema_up(&frame_sema);	
	// Yoonjae's QUESTION: 실패시 메모리 해제 안해도 됨 !?
	return false; // memory allocation failed
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init (&spt->pages, page_hash, page_less, NULL);	// TODO: page_hash, page_less 정의하기 (confluence 에서 복사)
}

// Customized hash_action_func
void page_copy (struct hash_elem *e, void *aux) {
	/* Yoonjae's TODO: 포인터로 넘겨주는 것들은 memcpy 로 새롭게 카피본 만들어서 넘겨줘야 할 것 같음 */
	struct page *p = hash_entry(e, struct page, hash_elem);
	enum vm_type type = VM_TYPE(p->operations->type);

	if(type == VM_UNINIT) {
		/* VM_ANON | VM_MARKER_0 이렇게 uninitialized page 로 만들어진 page 의 type 은 uninit.type 으로 참고
		 * initialize 실행 후에는 anon file 등으로 고정될 듯 */
		struct aux_load_segment *aux_copy = malloc(sizeof(struct aux_load_segment));
		if(aux_copy == NULL) exit(-1);
		memcpy(aux_copy, p->uninit.aux, sizeof(struct aux_load_segment));
		
		if(!vm_alloc_page_with_initializer(p->uninit.type, p->va, p->rw, p->uninit.init, aux_copy)) exit(-1);
	} else if(type == VM_ANON) {
		/* Yoonjae's Question:. vm_alloc_page VS vm_alloc_page(lazy_load) 둘 중 뭐가 맞을까? */
		if(!vm_alloc_page(p->operations->type, p->va, p->rw)) exit(-1);
		struct page *np = spt_find_page(&thread_current()->spt, p->va);
		vm_do_claim_page(np);
		memcpy(np->frame->kva, p->frame->kva, PGSIZE);
	}
	else if (type == VM_FILE) {
		struct aux_load_segment *aux_copy = malloc(sizeof(struct aux_load_segment));
		if(aux_copy == NULL) exit(-1);
		memcpy(aux_copy, p->file.aux, sizeof(struct aux_load_segment));

		if(!vm_alloc_page(p->operations->type, p->va, p->rw)) {
			free(aux_copy);
			exit(-1);
		}
		struct page *np = spt_find_page(&thread_current()->spt, p->va);

		vm_do_claim_page(np);
		np->file.file = file_reopen(aux_copy->file);
		np->file.aux = aux_copy;
		memcpy(np->frame->kva, p->frame->kva, PGSIZE);
	}
}; 

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	// hash_apply 이해하고,
	// hash_apply의 action 항에 copy하는 action 정의해서 넣기
	hash_apply(&src->pages, page_copy);
	// 우리가 아는 hash_apply: hash의 각 항을 순회하면서 각 항에 action을 적용
	// 2022.05.18
	// QUESTION: action으로 dst 어떻게 넘겨주나? -> aux 넘겨주듯이 src의 자료구조나 action_copy의 자료구조로 넘겨야하나?
	// action_copy (page, aux) { 
	// 	aux->dst
	// 	new_page = vm_alloc_page(page->type, page->va, page->rw);
	// 	vm_claim_page(new_page);
		// spt의 내용은 카피하지 않아도 되나?
	// }
	// copy는 uninit page를 할당하고, 바로 claim 해주는 것으로 한다
	// You will need to allocate uninit page and claim them immediately. (GitBook)
	return true;
}

/* action_copy 
 * 1. uninit copy
 * vm_alloc_page_with_initializer
 * 2. anon copy
 * You will need to allocate uninit page and claim them immediately.
 * vm_alloc_page_with_initializer 그리고 여기서 anon_initializer 
 * vm_claim_page 하고, 끝.
 * 3. file-backed copy 
 * vm_alloc_page_with_initializer 하고,
 * vm_claim_page.
 */

void page_kill (struct hash_elem *e, void *aux) {
	struct page *p = hash_entry(e, struct page, hash_elem);
	enum vm_type type = VM_TYPE(p->operations->type);
	// Yoonjae's TODO: VM_FILE 만 munmap / else destroy
	// spt_remove_page(&thread_current()->spt, p);
	destroy(p);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// hash_apply(spt->pages, action_destroy);
	// action_destroy(page, aux) { destroy(page) };
	// hash_apply의 action에 destroy하는 action 정의해서 넣기 (hash_destroy 이용 가능할듯? -> destroy(page))
	// action
	// elem (=page), aux 들어올텐데
	// destroy(page)
	// writeback all the modified contents to the storage.
	// dirty인 비트들 확인하고 
	// pml4가 dirty로 set 되어있으면 원래 storage에 복사
	// 그 뒤, dirty 해제
	hash_destroy(&spt->pages, page_kill);
}

/* 2022.05.18 
 * hash_destroy(, action_destroy)
 * action_destroy의 경우
 * pml4_is_dirty
 * file_write_at
 * pml4_is_dirty(, false)
 */
// destroy 하면 안에 있는 page들 다 dealloc? 
// vm_dealloc_page 쓸텐데 어떻게 쓸지 생각. 
// spt_remove_page ? 이거는 bool 함수이고, true 확인할 필요 없나?
// 디버그할때, spt_remove_page() != true -> 이상한거니까 체크할 수 있지 않나? 아니다.
// 결론: vm_dealloc_page 쓰자.
