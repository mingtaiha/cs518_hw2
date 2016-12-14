//	AUTHOR: MING TAI HA


#ifndef MY_ALLOCATE_H
#define MY_ALLOCATE_H

#include <unistd.h>
#include <sys/mman.h>

#define THREADREQ    0
#define MEMO_INIT    -1
#define CREATE_STACK -2
#define CREATE_HEAP  -3
#define SCHED_THREAD  -4

#define malloc(x) myallocate(x, __FILE__, __LINE__, THREADREQ)
#define free(x) mydeallocate(x, __FILE__, __LINE__, THREADREQ)

#define PMEM 8388608
#define SWP 16777612

typedef struct page_info {
	int thread_id;
	void* current_ptr;
	void* original_ptr;
	int swap_offset;
	struct page_info* next_page;
} page_info;

typedef struct thread_info {
	int thread_id;
	unsigned int mem_size;//heap_size
	page_info* stack;//stack
	page_info* head;//heap
	page_info* tail;
	struct thread_info* pre_thread;
	struct thread_info* next_thread;
} thread_info;

typedef struct free_list {
	page_info* head;
	page_info* tail;
} free_list;

typedef struct occupied_list {
    thread_info* head;
    thread_info* tail;
} occupied_list;

// Memory API
void * myallocate(unsigned int size, char * filename, int lineno, int req_id);
void mydeallocate(void * block, char * filename, int lineno, int req_id);

// Memory Auxiliary Functions: Block Level
void mem_init(void);
void * join_left(void * block);
void * join_right(void * block);
void * split(unsigned int req_size, void * block);

// Helper function that extends the page CONTIGUOUSLY by one page (4096)
void page_extend(void);

// Memory Auxiliary Functions: Doubly Circularly Linked List Level
void list_insert(void * block);
void * pick_to_remove(unsigned int size);
void block_remove(void * block);
void * bin_select(unsigned int size);


// Memory Auxiliary Functions: Misc
unsigned int size_roundup(unsigned int size);

void initialize_memory_management();
void*  initialize_new_thread(long int thr_id, unsigned int size);
thread_info* get_thr_info(long int thr_id);
void enqueue_occupied_list(thread_info* t);
thread_info* dequeue_occupied_list();
void enqueue_free_list(page_info* p);
page_info* dequeue_free_list();
void enqueue_thread_info(page_info* p, thread_info* t);
page_info* dequeue_thread_info(thread_info* t);
void del_ele_from_occupied_list(thread_info* t);
page_info* occupied_or_not(void* address);

occupied_list* o_list;
int pagesize;
#endif
